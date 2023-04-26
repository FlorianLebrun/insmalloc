#include <ins/memory/descriptors.h>
#include <ins/memory/controller.h>
#include <ins/timing.h>
#include <ins/os/memory.h>
#include <mutex>
#include <thread>
#include <iostream>
#include <condition_variable>

using namespace ins;
using namespace ins::mem;


struct OpaqueSchema : IObjectSchema {
   const char* name() override { return "<opaque>"; }
};

struct InvalidateSchema : IObjectSchema {
   const char* name() override { return "<invalid>"; }
};

constexpr uint32_t c_MaxTracker = 128;

namespace ins::mem {

   struct SchemaArena : ArenaDescriptor {
      std::mutex lock;
      uintptr_t alloc_region_size = 0;
      uintptr_t alloc_cursor = 0;
      uintptr_t alloc_commited = 0;
      uintptr_t alloc_end = 0;

      OpaqueSchema opaque_schema;
      InvalidateSchema invalidate_schema;

      SchemaArena();
      void Initialize();
      size_t GetTableUsedBytes();
      ObjectSchema CreateSchema(IObjectSchema* schema, uint32_t base_size, ObjectTraverser traverser, ObjectFinalizer finalizer);
   };

   struct HeapDescriptor : Descriptor {
      std::mutex notification_lock;
      std::condition_variable notification_signal;
      std::thread worker;
      bool terminating = false;

      std::mutex contexts_lock;
      uint16_t contexts_count = 0;
      MemoryContext* contexts = 0;

      MemoryCentralContext central;
      MemorySharedContext default_context;

      SchemaArena schemas;

      MemoryContext* recovered_contexts = 0;
      StarvedConsumerToken* starved_consumers = 0;

      ObjectAnalysisSession cleanup;
      uint32_t cycle = 0;

      std::mutex trackers_lock;
      uint16_t trackers_count = 0;
      ObjectReferenceTracker trackers[c_MaxTracker];


      HeapDescriptor();
      ~HeapDescriptor();

      void RunWorker();
      void NotifyWorker();
      void MarkUsedObjects();
      void SweepUnusedObjects();
   };
}

static mem::HeapDescriptor* controller = 0;

mem::SchemaArena::SchemaArena() {
   this->ArenaDescriptor::Initialize(cst::ArenaSizeL2);
   this->availables_count--;
   _ASSERT(this->availables_count == 0);
}

void mem::SchemaArena::Initialize() {
   address_t buffer = mem::ReserveArena();
   this->indice = buffer.arenaID;
   this->alloc_cursor = buffer;
   this->alloc_commited = buffer;
   this->alloc_end = buffer + cst::ArenaSize;
   this->alloc_region_size = size_t(1) << 16;
   mem::ArenaMap[buffer.arenaID] = this;

   mem::ObjectSchemas = ObjectSchema(this->GetBase());

   auto opaque_schema = CreateSchema(&this->opaque_schema, 0, 0, 0);
   if (GetObjectSchemaID(opaque_schema) != sObjectSchema::OpaqueID) throw;

   auto invalidate_schema = CreateSchema(&this->invalidate_schema, 0, 0, 0);
   if (GetObjectSchemaID(invalidate_schema) != sObjectSchema::InvalidateID) throw;
}

size_t mem::SchemaArena::GetTableUsedBytes() {
   return this->alloc_commited - this->GetBase();
}

ObjectSchema mem::SchemaArena::CreateSchema(IObjectSchema* infos, uint32_t base_size, ObjectTraverser traverser, ObjectFinalizer finalizer) {
   std::lock_guard<std::mutex> guard(this->lock);
   auto schema = ObjectSchema(this->alloc_cursor);
   this->alloc_cursor += sizeof(sObjectSchema);
   if (this->alloc_cursor > this->alloc_commited) {
      auto region_base = this->alloc_commited;
      this->alloc_commited += this->alloc_region_size;
      if (
         this->alloc_commited > this->alloc_end ||
         !os::CommitMemory(region_base, this->alloc_region_size))
      {
         throw "OOM";
         exit(1);
      }
   }
   schema->infos = infos;
   schema->base_size = base_size;
   schema->traverser = traverser;
   schema->finalizer = finalizer;
   return schema;
}

mem::HeapDescriptor::HeapDescriptor() {
   this->central.Initialize();
   this->schemas.Initialize();
   this->RunWorker();
}

void mem::HeapDescriptor::RunWorker() {
   this->worker = std::thread(
      [this]() {
         while (!this->terminating) {
            std::unique_lock<std::mutex> guard(this->notification_lock);
            this->notification_signal.wait(guard);
            if (this->terminating) break;

            auto starved_consumers = this->starved_consumers;
            auto recovered_contexts = this->recovered_contexts;
            this->starved_consumers = 0;
            this->recovered_contexts = 0;
            guard.unlock();

            // Apply memory soft recovery procedures
            while (auto context = recovered_contexts) {
               recovered_contexts = context->next.recovered;
               context->PerformCleanup();
               context->next.recovered = none<MemoryContext>();
            }

            // Apply memory hard recovery procedures
            if (starved_consumers) {

               // Cleanup heaps
               PerformHeapCleanup();

               // Restart starved consumers
               while (auto consumer = starved_consumers) {
                  starved_consumers = consumer->next;
                  consumer->next = 0;
                  consumer->signal.notify_one();
               }
            }
         }

      }
   );
}

mem::HeapDescriptor::~HeapDescriptor() {

   this->terminating = true;
   this->notification_signal.notify_all();
   this->worker.join();

   std::lock_guard<std::mutex> guard2(this->contexts_lock);
   while (this->contexts) {
      auto context = this->contexts;
      this->contexts = context->next.registered;
      if (context->allocated) {
         context->PerformCleanup();
         std::cerr << "Delete heap with alive contexts\n";
      }
      Descriptor::Delete(context);
   }
   this->central.PerformCleanup();
   mem::PerformRegionsCleanup();
}

void mem::HeapDescriptor::NotifyWorker() {
   this->notification_signal.notify_one();
}

void mem::HeapDescriptor::MarkUsedObjects() {
   this->cleanup.Reset();
   ObjectAnalysisSession::enabled = &this->cleanup;

   // Mark object from trackers
   {
      std::lock_guard<std::mutex> guard(controller->trackers_lock);
      for (uint32_t i = 0; i < controller->trackers_count; i++) {
         controller->trackers[i]->MarkObjects(*ObjectAnalysisSession::enabled);
      }
   }

   // Run marking of discovered object 
   this->cleanup.RunOnce();

   ObjectAnalysisSession::enabled = 0;
}

void mem::HeapDescriptor::SweepUnusedObjects() {
   size_t sweptObjects = 0;
   for (size_t i = 0; i < cst::ArenaPerSpace; i++) {
      auto& entry = mem::ArenaMap[i];
      if (entry.managed) {
         auto arena = entry.descriptor();
         address_t base = i << cst::ArenaSizeL2;

         // Compare aliveness map to the new aliveness snapshot
         auto alivenessSnapshot = &this->cleanup.regionAlivenessMap[this->cleanup.arenaIndexesMap[i]];
         auto startIndex = this->cleanup.arenaIndexesMap[i];
         auto endIndex = this->cleanup.arenaIndexesMap[i + 1];
         auto length = endIndex - startIndex;
         for (size_t i = 0; i < length; i++) {
            if (arena->regions[i].IsObjectRegion()) {
               auto region = ObjectRegion(base.ptr + (i << arena->segmentation));
               auto maskBits = cst::ObjectLayoutMask[region->layoutID];
               auto allocatedBits = (~(region->availables | region->notified_availables)) & maskBits;
               auto unusedBits = allocatedBits & (~alivenessSnapshot[i].flags);
               if (unusedBits) {
                  if (region->notified_availables.fetch_or(unusedBits) == 0) {
                     region->NotifyAvailables(true);
                  }
                  sweptObjects += bit::bitcount_64(unusedBits);
               }
            }
         }
      }
   }

   printf("sweep %lld objects\n", sweptObjects);
}

ObjectSchema mem::CreateObjectSchema(IObjectSchema* infos, uint32_t base_size, ObjectTraverser traverser, ObjectFinalizer finalizer) {
   return controller->schemas.CreateSchema(infos, base_size, traverser, finalizer);
}

void mem::PerformHeapCleanup() {
   timing::Chrono chrono;

   // Purge allocation caches
   for (auto context = controller->contexts; context; context = context->next.registered) {
      context->PerformCleanup();
   }
   controller->central.PerformCleanup();
   mem::PerformRegionsCleanup();

   // Sweep unused objects
   mem::MarkAndSweepUnusedObjects();

   printf("> cleanup time: %g ms\n", chrono.GetDiffFloat(chrono.MS));
}

void mem::MarkAndSweepUnusedObjects() {
   if (ObjectAnalysisSession::running.try_lock()) {
      _ASSERT(!ObjectAnalysisSession::enabled);
      controller->cycle++;

      // Run objects aliveness analysis
      controller->MarkUsedObjects();

      // Sweep unused objects
      controller->SweepUnusedObjects();

      ObjectAnalysisSession::running.unlock();
   }
}

void mem::RegisterReferenceTracker(ObjectReferenceTracker tracker) {
   std::lock_guard<std::mutex> guard(controller->trackers_lock);
   if (controller->trackers_count < c_MaxTracker) {
      controller->trackers[controller->trackers_count] = tracker;
      controller->trackers_count++;
   }
   else {
      throw "MaxTracker limit reach";
   }
}

void mem::UnregisterReferenceTracker(ObjectReferenceTracker tracker) {
   std::lock_guard<std::mutex> guard(controller->trackers_lock);
   uint32_t index = 0;
   while (index < controller->trackers_count) {
      if (controller->trackers[index] == tracker) {
         controller->trackers[index] = controller->trackers[controller->trackers_count - 1];
         controller->trackers_count--;
      }
      else index++;
   }
}

void mem::CheckValidity() {
   std::lock_guard<std::mutex> guard(controller->contexts_lock);
   for (auto context = controller->contexts; context; context = context->next.registered) {
      context->CheckValidity();
   }
}

void tObjectsStats::add(ObjectRegion region) {
   this->region_count++;

   this->used_bytes += region->GetUsedCount() * region->GetObjectSize();
   this->notified_bytes += region->GetNotifiedCount() * region->GetObjectSize();
   this->avaiblable_bytes += region->GetAvailablesCount() * region->GetObjectSize();
   this->total_bytes += region->GetRegionSize();

   this->used_objects += region->GetUsedCount();
   this->notified_objects += region->GetNotifiedCount();
   this->avaiblable_objects += region->GetAvailablesCount();
   this->total_objects += region->GetCount();
}

void tObjectsStats::add(tObjectsStats& stat) {
   this->region_count += stat.region_count;

   this->used_bytes += stat.used_bytes;
   this->notified_bytes += stat.notified_bytes;
   this->avaiblable_bytes += stat.avaiblable_bytes;
   this->total_bytes += stat.total_bytes;

   this->used_objects += stat.used_objects;
   this->notified_objects += stat.notified_objects;
   this->avaiblable_objects += stat.avaiblable_objects;
   this->total_objects += stat.total_objects;
}

void tObjectsStats::print() {
   printf("\n|  - bytes used       : %s", sz2a(used_bytes).c_str());
   printf("\n|  - bytes notified   : %s", sz2a(notified_bytes).c_str());
   printf("\n|  - bytes avaiblable : %s", sz2a(avaiblable_bytes).c_str());
   printf("\n|  - bytes            : %s", sz2a(total_bytes).c_str());
   printf("\n|");

   printf("\n|  - objects used       : %lld", used_objects);
   printf("\n|  - objects avaiblable : %lld", avaiblable_objects);
   printf("\n|  - objects notified   : %lld", notified_objects);
   printf("\n|  - objects            : %lld", total_objects);
   printf("\n|");

   printf("\n|  - region count : %llu", region_count);
   printf("\n|");
}

tObjectsStats mem::GetObjectsStats() {
   tObjectsStats stats;
   controller->central.ForeachObjectRegion(
      [&](ObjectRegion region) {
         stats.add(region);
         return true;
      }
   );
   return stats;
}

void mem::PrintInfos() {
   std::lock_guard<std::mutex> guard(controller->contexts_lock);

   tObjectsStats central_stat;
   auto cstats_length = controller->contexts_count;
   auto* cstats = new (alloca(sizeof(tObjectsStats) * cstats_length)) tObjectsStats[cstats_length];
   controller->central.ForeachObjectRegion(
      [&](ObjectRegion region) {
         auto ctx = region->owner ? region->owner->context : 0;
         auto& stat = ctx ? cstats[ctx->id] : central_stat;
         stat.add(region);
         return true;
      }
   );

   printf("------------------- MemoryCentralContext -------------------\n");
   tObjectsStats global_stat;
   for (auto context = controller->contexts; context; context = context->next.registered) {
      auto& cstat = cstats[context->id];
      global_stat.add(cstat);
      printf("| Context %d (0x%p) ", context->id, context);
      if (!context->allocated) printf(" [free]");
      if (context->isShared) printf(" [shared]");
      if (context->thread) {
         printf("\n|  - threadId: %llu", context->thread.GetID());
      }
      printf("\n|");
      cstat.print();
      printf("\n|-------------------------------------------------");
      printf("\n");
   }
   {
      global_stat.add(central_stat);
      printf("| Central");
      central_stat.print();
      printf("\n|-------------------------------------------------");
      printf("\n");
   }
   {
      printf("| Heap:");
      printf("\n|  - context count: %d", cstats_length);
      global_stat.print();
      printf("\n");
   }
   {
      auto space_stats = mem::GetMemoryStats();
      printf("| Technical:");
      printf("\n|  - schema table : %s", sz2a(controller->schemas.GetTableUsedBytes()).c_str());
      printf("\n|  - descriptors  : %s", sz2a(space_stats.descriptors_used_bytes).c_str());
      printf("\n|  - arenas_map  : %s", sz2a(space_stats.arenas_map_used_bytes).c_str());
      printf("\n|  - total  : %s", sz2a(space_stats.used_bytes).c_str());
      printf("\n");
   }
   printf("--------------------------------------------------\n");
}

void mem::InitializeHeap() {
   if (!controller) {
      mem::InitializeMemory();

      controller = Descriptor::New<HeapDescriptor>();
      mem::Central = &controller->central;

      controller->default_context.AcquireContext();
      mem::DefaultContext = &controller->default_context;
   }
}

mem::MemoryCentralContext& mem::AcquireCentralContext() {
   mem::InitializeHeap();
   return controller->central;
}

mem::MemoryContext* mem::AcquireContext(bool isShared) {
   {
      std::lock_guard<std::mutex> guard(controller->contexts_lock);
      for (auto context = controller->contexts; context; context = context->next.registered) {
         if (!context->allocated) {
            context->allocated = true;
            return context;
         }
      }
   }
   {
      auto context = Descriptor::New<MemoryContext>();
      context->allocated = true;
      context->isShared = isShared;
      mem::Central->InitiateContext(context);

      std::lock_guard<std::mutex> guard(controller->contexts_lock);
      context->next.registered = controller->contexts;
      context->id = controller->contexts_count++;
      controller->contexts = context;
      return context;
   }
}

void mem::DisposeContext(MemoryContext* _context) {
   auto context = static_cast<mem::MemoryContext*>(_context);
   if (context->isShared) {
      throw "shared cannot be disposed";
   }
   if (context->allocated) {
      context->allocated = false;
   }
}

void mem::SetTimeStampOption(bool enabled) {
   std::lock_guard<std::mutex> guard(controller->contexts_lock);
   ObjectAllocOptions options;
   options.enableTimeStamp = 1;
   if (enabled) {
      for (auto ctx = controller->contexts; ctx; ctx = ctx->next.registered)
         ctx->options.enableds |= options.enableds;
   }
   else {
      for (auto ctx = controller->contexts; ctx; ctx = ctx->next.registered)
         ctx->options.enableds &= ~options.enableds;
   }
}

void mem::SetStackStampOption(bool enabled) {
   std::lock_guard<std::mutex> guard(controller->contexts_lock);
   ObjectAllocOptions options;
   options.enableStackStamp = 1;
   if (enabled) {
      for (auto ctx = controller->contexts; ctx; ctx = ctx->next.registered)
         ctx->options.enableds |= options.enableds;
   }
   else {
      for (auto ctx = controller->contexts; ctx; ctx = ctx->next.registered)
         ctx->options.enableds &= ~options.enableds;
   }
}

void mem::SetSecurityPaddingOption(uint32_t paddingSize) {
   std::lock_guard<std::mutex> guard(controller->contexts_lock);
   for (auto context = controller->contexts; context; context = context->next.registered) {
      context->options.enableSecurityPadding = paddingSize;
   }
}

void mem::RescueStarvedConsumer(StarvedConsumerToken& token) {
   {
      std::lock_guard<std::mutex> guard(controller->notification_lock);
      token.next = controller->starved_consumers;
      controller->starved_consumers = &token;
   }
   std::unique_lock<std::mutex> guard(token.lock);
   controller->NotifyWorker();
   token.signal.wait(guard);
}

void mem::ScheduleContextRecovery(MemoryContext* context) {
   if (context->next.recovered == none<MemoryContext>()) {
      {
         std::lock_guard<std::mutex> guard(controller->notification_lock);
         if (context->next.recovered == none<MemoryContext>()) {
            context->next.recovered = controller->recovered_contexts;
            controller->recovered_contexts = context;
         }
         else {
            return; // already scheduled
         }
      }
      controller->NotifyWorker();
   }
}

void mem::NotifyHeapIssue(tHeapIssue issue, address_t addr) {
   switch (issue) {
   case tHeapIssue::FreeOutOfBoundObject: {
      printf("! FreeOutOfBoundObject at 0x%p\n", addr.as<void>());
   } break;
   case tHeapIssue::FreeInexistingObject: {
      printf("! FreeInexistingObject at 0x%p\n", addr.as<void>());
   } break;
   case tHeapIssue::FreeRetainedObject: {
      printf("! FreeRetainedObject at 0x%p\n", addr.as<void>());
   }break;
   }
}
