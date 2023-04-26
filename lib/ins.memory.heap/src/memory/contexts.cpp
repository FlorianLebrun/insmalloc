#include <ins/memory/contexts.h>
#include <ins/memory/schemas.h>
#include <ins/memory/controller.h>
#include <ins/memory/controller.h>
#include <ins/os/threading.h>

using namespace ins;
using namespace ins::mem;

mem::MemoryCentralContext* mem::Central = 0;
mem::MemorySharedContext* mem::DefaultContext = 0;
_declspec(thread) MemoryContext* mem::CurrentContext = 0;

void mem::MemorySharedContext::AcquireContext() {
   this->shared = mem::AcquireContext(true);
}

/**********************************************************************
*
*   MemoryCentralContext
*
***********************************************************************/

void mem::MemoryCentralContext::Initialize() {
   this->unmanaged.Initialize(false);
   this->managed.Initialize(true);
}

void mem::MemoryCentralContext::CheckValidity() {
   this->unmanaged.CheckValidity();
   this->managed.CheckValidity();
}

void mem::MemoryCentralContext::ForeachObjectRegion(std::function<bool(ObjectRegion)>&& visitor) {
   mem::ForeachRegion(
      [&](ArenaDescriptor* arena, RegionLayoutID layout, address_t addr) {
         if (layout.IsObjectRegion()) {
            return visitor(ObjectRegion(addr.ptr));
         }
         return true;
      }
   );
}

void mem::MemoryCentralContext::PerformCleanup() {
   this->unmanaged.Clean();
   this->managed.Clean();
}

void mem::MemoryCentralContext::InitiateContext(MemoryContext* context) {
   context->unmanaged.Initialize(context, &this->unmanaged);
   context->managed.Initialize(context, &this->managed);
}

/**********************************************************************
*
*   MemoryContext
*
***********************************************************************/

void mem::MemoryContext::RescueStarvingSituation(size_t expectedByteLength) {
   mem::StarvedConsumerToken token;
   token.expectedByteLength = expectedByteLength;
   token.context = mem::CurrentContext;
   mem::RescueStarvedConsumer(token);
}

void mem::MemoryContext::CheckValidity() {
   //this->unmanaged.CheckValidity();
   //this->managed.CheckValidity();
}

void* mem::MemoryContext::AllocateUnmanaged(ObjectSchemaID schema_id, size_t size) {
   ObjectHeader obj;
   size += sizeof(sObjectHeader);
   if (!options.enableds) obj = this->unmanaged.AllocateObject(size);
   else obj = this->unmanaged.AllocateInstrumentedObject(size, this->options);
   obj->schema_id = schema_id;
   return &obj[1];
}

void* mem::MemoryContext::AllocateManaged(ObjectSchemaID schema_id, size_t size) {
   ObjectHeader obj;
   size += sizeof(sObjectHeader);
   if (!options.enableds) obj = this->managed.AllocateObject(size);
   else obj = this->managed.AllocateInstrumentedObject(size, this->options);
   obj->schema_id = schema_id;
   if (auto session = ObjectAnalysisSession::enabled) {
      session->MarkPtr(obj);
   }
   return &obj[1];
}

void** mem::MemoryContext::NewHardReference(void* ptr) {
   return 0;
}

void** mem::MemoryContext::NewWeakReference(void* ptr) {
   return 0;
}

void mem::MemoryContext::Scavenge() {
   printf("> Scavenge context %d [%s]\n", this->id, this->isShared ? "shared" : "private");
   this->unmanaged.Scavenge();
   this->managed.Scavenge();
}

void mem::MemoryContext::PerformCleanup() {
   if (this->thread.IsCurrent()) {
      this->Scavenge();
   }
   else if (this->owning.try_lock()) {
      std::lock_guard<std::mutex> guard(this->owning, std::adopt_lock);
      this->Scavenge();
   }
   else if (this->thread) {
      this->thread.Suspend();
      this->Scavenge();
      this->thread.Resume();
   }
}

/**********************************************************************
*
*   MemoryContext
*
***********************************************************************/

MemoryContext* mem::GetThreadContext() {
   return mem::CurrentContext;
}

MemoryContext* mem::SetThreadContext(MemoryContext* context) {
   auto prev = mem::CurrentContext;
   mem::CurrentContext = context;
   return prev;
}

void* mem::AllocateObject(size_t size) {
   if (auto context = mem::CurrentContext) return context->AllocateUnmanaged(0, size);
   else return mem::DefaultContext->AllocateUnmanaged(0, size);
}

void* mem::AllocateUnmanagedObject(ObjectSchemaID schemaID, size_t size) {
   if (auto context = mem::CurrentContext) return context->AllocateUnmanaged(schemaID, size);
   else return mem::DefaultContext->AllocateUnmanaged(schemaID, size);
}

void* mem::AllocateManagedObject(ObjectSchemaID schemaID, size_t size) {
   if (auto context = mem::CurrentContext) return context->AllocateManaged(schemaID, size);
   else return mem::DefaultContext->AllocateManaged(schemaID, size);
}

void* mem::AllocateUnmanagedObject(ObjectSchemaID schemaID) {
   auto schema = mem::GetObjectSchema(schemaID);
   if (auto context = mem::CurrentContext) return context->AllocateUnmanaged(schemaID, schema->base_size);
   else return mem::DefaultContext->AllocateUnmanaged(schemaID, schema->base_size);
}

void* mem::AllocateManagedObject(ObjectSchemaID schemaID) {
   auto schema = mem::GetObjectSchema(schemaID);
   if (auto context = mem::CurrentContext) return context->AllocateManaged(schemaID, schema->base_size);
   else return mem::DefaultContext->AllocateManaged(schemaID, schema->base_size);
}

void mem::RetainObject(void* ptr) {
   ObjectLocation(ptr).Retain();
}

void mem::RetainObjectWeak(void* ptr) {
   ObjectLocation(ptr).RetainWeak();
}

bool mem::ReleaseObject(void* ptr) {
   return ObjectLocation(ptr).Release(mem::CurrentContext);
}

bool mem::ReleaseObjectWeak(void* ptr) {
   return ObjectLocation(ptr).ReleaseWeak(mem::CurrentContext);
}

bool mem::FreeObject(void* ptr) {
   return ObjectLocation(ptr).Free(mem::CurrentContext);
}


void** mem::NewHardReference(void* ptr) {
   if (auto context = mem::CurrentContext) return context->NewHardReference(ptr);
   else return mem::DefaultContext->NewHardReference(ptr);
}

void** mem::NewWeakReference(void* ptr) {
   if (auto context = mem::CurrentContext) return context->NewWeakReference(ptr);
   else return mem::DefaultContext->NewWeakReference(ptr);
}

void mem::DeleteReference(void** ref) {
   throw "TODO";
}
