#include <ins/memory/contexts.h>
#include <ins/memory/schemas.h>
#include <ins/memory/space.h>
#include <ins/memory/objects-refs.h>
#include <ins/memory/controller.h>
#include <ins/os/threading.h>
#include <ins/timing.h>

using namespace ins;

namespace ins {
   _declspec(thread) MemoryContext* context = 0;
}

MemoryLocalSite::MemoryLocalSite(void* ptr) : ptr(ptr) {
   auto ctx = ins::context;
   this->pprev = &ctx->locals;
   this->next = ctx->locals;
   ctx->locals = this;
}

MemoryLocalSite::~MemoryLocalSite() {
   this->pprev[0] = this->next;
}

ThreadDedicatedContext::ThreadDedicatedContext(MemoryContext* context, bool disposable) {
   this->Put(context, disposable);
}

ThreadDedicatedContext::~ThreadDedicatedContext() {
   bool disposable = this->disposable;
   auto context = this->Pop();
   if (context && disposable) {
      ins::Controller.DisposeContext(context);
   }
}

MemoryContext& ThreadDedicatedContext::operator *() {
   return *ins::context;
}

MemoryContext* ThreadDedicatedContext::operator ->() {
   return ins::context;
}

void ThreadDedicatedContext::Put(MemoryContext* context, bool disposable) {
   if (ins::context) throw "previous context shall be released";
   if (!context) {
      context = ins::Controller.AcquireContext();
      this->disposable = true;
   }
   else {
      this->disposable = disposable;
   }
   if (!context->owning.try_lock()) {
      throw "context already owned";
   }
   context->thread = ins::OS::Thread::current();
   ins::context = context;
}

MemoryContext* ThreadDedicatedContext::Pop() {
   auto context = ins::context;
   if (context) {
      context->thread.Clear();
      ins::context->owning.unlock();
      ins::context = 0;
   }
   return context;
}

MemoryContext* ins_get_thread_context() {
   return ins::context;
}

void* ins_malloc(size_t size) {
   if (auto context = ins::context) return context->NewPrivatedUnmanaged(size + sizeof(sObjectHeader));
   else return ins::Controller.defaultContext->NewPrivatedUnmanaged(size + sizeof(sObjectHeader));
}

void ins_free(void* ptr) {
   if (auto context = ins::context) return context->FreeObject(ptr);
   else return ins::Controller.defaultContext->FreeObject(ptr);
}

ObjectHeader ins_new(size_t size) {
   if (auto context = ins::context) return context->NewPrivatedUnmanaged(size);
   else return ins::Controller.defaultContext->NewPrivatedUnmanaged(size);
}

ins::ObjectHeader ins_new_unmanaged(ins::SchemaID schemaID) {
   ObjectHeader obj;
   auto desc = ins::schemasHeap.GetSchemaDesc(schemaID);
   if (auto context = ins::context) obj = context->NewPrivatedUnmanaged(desc->base_size);
   else obj = ins::Controller.defaultContext->NewPrivatedUnmanaged(desc->base_size);
   obj->schemaID = schemaID;
   return obj;
}

ins::ObjectHeader ins_new_managed(ins::SchemaID schemaID) {
   ObjectHeader obj;
   auto desc = ins::schemasHeap.GetSchemaDesc(schemaID);
   if (auto context = ins::context) obj = context->NewPrivatedManaged(desc->base_size);
   else obj = ins::Controller.defaultContext->NewPrivatedManaged(desc->base_size);
   if (auto session = ObjectAnalysisSession::enabled) {
      session->MarkPtr(obj);
   }
   obj->schemaID = schemaID; // note: mark before schema setup
   return obj;
}

void ins_delete(ObjectHeader obj) {
   if (auto context = ins::context) return context->FreeObject(obj);
   else return ins::Controller.defaultContext->FreeObject(obj);
}

void* ins_calloc(size_t count, size_t size) {
   return malloc(count * size);
}

bool ins_check_overflow(void* ptr) {
   ObjectInfos infos(ptr);
   return infos.detectOverflowedBytes() == 0;
}

bool ins_get_metadata(void* ptr, ins::ObjectAnalyticsInfos& meta) {
   ObjectInfos infos(ptr);
   if (auto pmeta = infos.getAnalyticsInfos()) {
      meta = *pmeta;
      return true;
   }
   return false;
}

size_t ins_msize(void* ptr, tp_ins_msize default_msize) {
   ObjectInfos infos(ptr);
   if (infos.object) {
      return infos.usable_size();
   }
   else if (default_msize) {
      return default_msize(ptr);
   }
   return 0;
}

void* ins_realloc(void* ptr, size_t size, tp_ins_realloc default_realloc) {
   ObjectInfos infos(ptr);
   if (infos.object) {
      if (size == 0) {
         ins_free(ptr);
         return 0;
      }
      else if (size > infos.usable_size()) {
         void* new_ptr = ins_malloc(size);
         memcpy(new_ptr, ptr, infos.usable_size());
         ins_free(ptr);
         return new_ptr;
      }
      else {
         return ptr;
      }
   }
   else {
      if (default_realloc) {
         return default_realloc(ptr, size);
      }
      else {
         void* new_ptr = ins_malloc(size);
         __try { memcpy(new_ptr, ptr, size); }
         __except (1) {}
         printf("sat cannot realloc unkown buffer\n");
         return new_ptr;
      }
   }
   return 0;
}

/**********************************************************************
*
*   MemoryCentralContext
*
***********************************************************************/

void MemoryCentralContext::Initiate() {
   this->unmanaged.Initiate(false);
   this->managed.Initiate(true);
}

void MemoryCentralContext::CheckValidity() {
   this->unmanaged.CheckValidity();
   this->managed.CheckValidity();
}

void MemoryCentralContext::ForeachObjectRegion(std::function<bool(ObjectRegion)>&& visitor) {
   ins::RegionsHeap.ForeachRegion(
      [&](ArenaDescriptor* arena, RegionLayoutID layout, address_t addr) {
         if (layout.IsObjectRegion()) {
            return visitor(ObjectRegion(addr.ptr));
         }
         return true;
      }
   );
}

void ins::MemoryCentralContext::PerformMemoryCleanup() {
   this->unmanaged.Clean();
   this->managed.Clean();
}

/**********************************************************************
*
*   ObjectLocalContext
*
***********************************************************************/

void ObjectLocalContext::LocalObjects::RemoveActiveRegion() {
   if (this->active_region) {
      auto region = this->active_region;
      this->active_region = 0;
      this->usables.Push(region);
   }
}

void ObjectLocalContext::Initiate(MemoryContext* context, ObjectCentralContext* heap) {
   this->managed = heap->managed;
   this->context = context;
   this->heap = heap;
}

void ObjectLocalContext::Clean() {
   for (int layoutID = 0; layoutID < cst::ObjectLayoutCount; layoutID++) {
      auto& privated = this->privateds[layoutID];
      auto& shared = this->shareds[layoutID];
      auto& central = this->heap->objects[layoutID];

      // Scavenge owned regions before dump
      this->ScavengeNotifiedRegions(layoutID);

      // Clean usables regions
      privated.RemoveActiveRegion();
      privated.usables.CollectDisposables(this->disposables[layoutID]);
      shared.RemoveActiveRegion();
      shared.usables.CollectDisposables(this->disposables[layoutID]);

      // Dump all not full to central pool
      std::lock_guard<std::mutex> guard(central.lock);
      privated.usables.DumpInto(central.usables, 0);
      shared.usables.DumpInto(central.usables, 0);
      this->disposables[layoutID].DumpInto(central.disposables, 0);
   }
}

ObjectHeader ObjectLocalContext::AllocatePrivatedObject(size_t size) {
   auto objectLayoutID = getLayoutForSize(size);
   _ASSERT(size <= ins::cst::ObjectLayoutBase[objectLayoutID].object_multiplier);
   if (objectLayoutID < cst::ObjectLayoutMax) {
      return this->AcquirePrivatedObject(objectLayoutID);
   }
   else {
      return this->AllocateLargeObject(size, true);
   }
}

ObjectHeader ObjectLocalContext::AllocateSharedObject(size_t size) {
   auto objectLayoutID = getLayoutForSize(size);
   _ASSERT(size <= ins::cst::ObjectLayoutBase[objectLayoutID].object_multiplier);
   if (objectLayoutID < cst::ObjectLayoutMax) {
      return this->AcquireSharedObject(objectLayoutID);
   }
   else {
      return this->AllocateLargeObject(size, false);
   }
}

ObjectHeader ObjectLocalContext::AllocateLargeObject(size_t size, bool privated) {
   auto region = sObjectRegion::New(this->managed, cst::ObjectLayoutMax, size, this);
   auto obj = region->GetObjectAt(0);
   region->privated = privated;
   return obj;
}

ObjectHeader ObjectLocalContext::AllocateInstrumentedObject(size_t size, bool privated, ObjectAllocOptions options) {
   size_t requiredSize = size + options.enableSecurityPadding;
   if (options.enableAnalytics) requiredSize += sizeof(ObjectAnalyticsInfos);
   auto objectLayoutID = getLayoutForSize(requiredSize);

   // Allocate memory
   ObjectHeader obj = 0;
   if (objectLayoutID < cst::ObjectLayoutMax) {
      if (privated) obj = this->AcquirePrivatedObject(objectLayoutID);
      else obj = this->AcquireSharedObject(objectLayoutID);
   }
   else {
      obj = this->AllocateLargeObject(size, privated);
   }

   // Configure analytics infos
   uint32_t bufferLen = ins::cst::ObjectLayoutBase[objectLayoutID].object_multiplier;
   if (options.enableAnalytics) {
      bufferLen -= sizeof(ObjectAnalyticsInfos);
      auto infos = (ObjectAnalyticsInfos*)&ObjectBytes(obj)[bufferLen];
      infos->timestamp = ins::timing::getCurrentTimestamp();
      infos->stackstamp = 42;
      obj->hasAnalyticsInfos = 1;
   }

   // Configure security padding
   if (options.enableSecurityPadding) {
      uint8_t* paddingBytes = &ObjectBytes(obj)[size];
      uint32_t paddingLen = bufferLen - size - sizeof(uint32_t);
      uint32_t& paddingTag = (uint32_t&)paddingBytes[paddingLen];
      paddingTag = size ^ 0xabababab;
      memset(paddingBytes, 0xab, paddingLen);
      obj->hasSecurityPadding = 1;
   }

   return obj;
}

/**********************************************************************
*
*   MemoryContext
*
***********************************************************************/

void MemoryContext::Initiate(MemoryCentralContext* heap) {
   this->heap = heap;
   this->unmanaged.Initiate(this, &heap->unmanaged);
   this->managed.Initiate(this, &heap->managed);
}

void MemoryContext::CheckValidity() {
   //this->unmanaged.CheckValidity();
   //this->managed.CheckValidity();
}

void MemoryContext::MarkUsedObject() {
   for (auto loc = this->locals; loc; loc = loc->next) {
      ObjectAnalysisSession::enabled->MarkPtr(loc->ptr);
   }
}

ObjectHeader MemoryContext::NewPrivatedUnmanaged(size_t size) {
   if (!options.enableds) {
      return this->unmanaged.AllocatePrivatedObject(size);
   }
   else {
      return this->unmanaged.AllocateInstrumentedObject(size, true, this->options);
   }
}

ObjectHeader MemoryContext::NewPrivatedManaged(size_t size) {
   if (!options.enableds) {
      return this->managed.AllocatePrivatedObject(size);
   }
   else {
      return this->managed.AllocateInstrumentedObject(size, true, this->options);
   }
}

ObjectHeader MemoryContext::NewSharedUnmanaged(size_t size) {
   if (!options.enableds) {
      return this->unmanaged.AllocateSharedObject(size);
   }
   else {
      return this->unmanaged.AllocateInstrumentedObject(size, false, this->options);
   }
}

ObjectHeader MemoryContext::NewSharedManaged(size_t size) {
   if (!options.enableds) {
      return this->managed.AllocateSharedObject(size);
   }
   else {
      return this->managed.AllocateInstrumentedObject(size, false, this->options);
   }
}


void MemoryContext::FreeObject(address_t address) {
   ObjectLocation location(address);
   if (location.object) {
      auto region = ObjectRegion(location.region);
      auto owner = location.arena.managed ? &this->managed : &this->unmanaged;

      // Check object is not available
      auto object_bit = uint64_t(1) << location.index;
      if (region->availables & object_bit ||
         region->notified_availables.load(std::memory_order_relaxed) & object_bit)
      {
         throw "Cannot free not allocated object";
      }

      // Release object to region owner
      if (region->owner == owner) {
         if (region->availables == 0) {
            owner->PushUsableRegion(region);
         }
         region->availables |= object_bit;
         _ASSERT(region->availables != 0);
      }
      else {
         if (region->notified_availables.fetch_or(object_bit) == 0) {
            if (region->owner) {
               if (region->privated) {
                  auto count = region->owner->privateds[region->layoutID].notifieds.Push(region);
                  if (count > 10) {
                     ins::Controller.ScheduleContextRecovery(region->owner->context);
                  }
               }
               else {
                  auto count = region->owner->shareds[region->layoutID].notifieds.Push(region);
                  if (count > 10) {
                     ins::Controller.ScheduleContextRecovery(region->owner->context);
                  }
               }
            }
            else {
               auto list = location.arena.managed ? ins::Controller.central.managed.objects : ins::Controller.central.unmanaged.objects;
               list[region->layoutID].notifieds.Push(region);
            }
         }
      }
   }
}

void MemoryContext::PerformMemoryCleanup() {

   auto PerformCleanup = [this]() {
      printf("Clean context\n");
      this->unmanaged.Clean();
      this->managed.Clean();
   };

   if (this->thread.IsCurrent()) {
      PerformCleanup();
   }
   else if (this->owning.try_lock()) {
      std::lock_guard<std::mutex> guard(this->owning, std::adopt_lock);
      PerformCleanup();
   }
   else if (this->thread) {
      this->thread.Suspend();
      PerformCleanup();
      this->thread.Resume();
   }
}
