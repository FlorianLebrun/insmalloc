#pragma once
#include <ins/memory/map.h>
#include <ins/memory/objects-base.h>
#include <ins/memory/objects-pool.h>
#include <ins/memory/schemas.h>
#include <ins/os/threading.h>

namespace ins::mem {

   struct MemoryContext : IMemoryConsumer {
      ObjectAllocOptions options;

      uint16_t id = 0;

      std::mutex owning;
      ins::os::Thread thread;
      uint8_t allocated : 1;
      uint8_t isShared : 1;

      ObjectLocalContext unmanaged;
      ObjectLocalContext managed;

      struct {
         MemoryContext* registered = none<MemoryContext>();
         MemoryContext* recovered = none<MemoryContext>();
      } next;


      void* AllocateUnmanaged(ObjectSchemaID schema_id, size_t size);
      void* AllocateManaged(ObjectSchemaID schema_id, size_t size);

      void** NewHardReference(void* ptr);
      void** NewWeakReference(void* ptr);

      void PerformCleanup();
      void CheckValidity();

   protected:
      void Scavenge();
      void RescueStarvingSituation(size_t expectedByteLength) override;
   };

   struct MemorySharedContext {
   private:
      MemoryContext* shared = 0;
   public:
      void AcquireContext();
      void CheckValidity() {
         std::lock_guard<std::mutex> guard(this->shared->owning);
         return this->shared->CheckValidity();
      }
      void* AllocateUnmanaged(ObjectSchemaID schema_id, size_t size) {
         std::lock_guard<std::mutex> guard(this->shared->owning);
         return this->shared->AllocateUnmanaged(schema_id, size);
      }
      void* AllocateManaged(ObjectSchemaID schema_id, size_t size) {
         std::lock_guard<std::mutex> guard(this->shared->owning);
         return this->shared->AllocateManaged(schema_id, size);
      }
      void** NewHardReference(void* ptr) {
         std::lock_guard<std::mutex> guard(this->shared->owning);
         return this->shared->NewHardReference(ptr);
      }
      void** NewWeakReference(void* ptr) {
         std::lock_guard<std::mutex> guard(this->shared->owning);
         return this->shared->NewWeakReference(ptr);
      }
   };

   struct MemoryCentralContext {
      ObjectCentralContext unmanaged;
      ObjectCentralContext managed;
      ObjectAllocOptions options;

      void Initialize();
      void CheckValidity();
      void ForeachObjectRegion(std::function<bool(ObjectRegion)>&& visitor);

      void PerformCleanup();

      void InitiateContext(MemoryContext* context);
   };

   // Context API
   extern _declspec(thread) MemoryContext* CurrentContext;
   extern MemorySharedContext* DefaultContext;
   extern MemoryCentralContext* Central;
   extern MemoryContext* GetThreadContext();
   extern MemoryContext* SetThreadContext(MemoryContext* context);

   // Object allocation API
   extern void* AllocateObject(size_t size);
   extern void* AllocateUnmanagedObject(ObjectSchemaID schemaID, size_t size);
   extern void* AllocateManagedObject(ObjectSchemaID schemaID, size_t size);
   extern void* AllocateUnmanagedObject(ObjectSchemaID schemaID);
   extern void* AllocateManagedObject(ObjectSchemaID schemaID);

   // Object retention API
   extern void RetainObject(void* ptr);
   extern bool ReleaseObject(void* ptr);
   extern bool FreeObject(void* ptr);

   // Weak object retention API
   extern void RetainObjectWeak(void* ptr);
   extern bool ReleaseObjectWeak(void* ptr);

   // Managed object reference API
   extern void** NewHardReference(void* ptr);
   extern void** NewWeakReference(void* ptr);
   extern void DeleteReference(void** ref);
}
