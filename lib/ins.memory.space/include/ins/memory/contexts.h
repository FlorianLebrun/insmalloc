#pragma once
#include <ins/memory/regions.h>
#include <ins/memory/schemas.h>
#include <ins/memory/objects-base.h>
#include <ins/memory/objects-pool.h>
#include <ins/os/threading.h>

namespace ins {

   struct MemoryContext;
   struct MemorySharedContext;
   struct MemoryCentralContext;

   struct MemoryLocalSite {
   private:
      MemoryLocalSite** pprev;
      MemoryLocalSite* next;
      friend class MemoryCentralContext;
      friend class MemoryContext;
   public:
      void* ptr;
      MemoryLocalSite(void* ptr = 0);
      ~MemoryLocalSite();
   };

   struct MemoryContext {
      MemoryCentralContext* heap;
      ObjectAllocOptions options;

      std::mutex owning;
      ins::OS::Thread thread;
      MemoryLocalSite* locals = 0;
      uint16_t id = 0;

      ObjectLocalContext unmanaged;
      ObjectLocalContext managed;

      ObjectHeader NewPrivatedUnmanaged(size_t size);
      ObjectHeader NewPrivatedManaged(size_t size);
      ObjectHeader NewSharedUnmanaged(size_t size);
      ObjectHeader NewSharedManaged(size_t size);
      void FreeObject(address_t address);

      void PerformMemoryCleanup();
      void MarkUsedObject();
      void CheckValidity();

      struct {
         MemoryContext* registered = none<MemoryContext>();
         MemoryContext* recovered = none<MemoryContext>();
      } next;

   protected:
      bool allocated = false;
      bool isShared = false;
      friend struct Descriptor;
      friend struct MemoryController;
      void Initiate(MemoryCentralContext* heap);
   };

   struct MemorySharedContext : protected MemoryContext {
      MemorySharedContext() {
         this->isShared = true;
      }
      void CheckValidity() {
         std::lock_guard<std::mutex> guard(this->owning);
         return this->MemoryContext::CheckValidity();
      }
      ObjectHeader NewPrivatedUnmanaged(size_t size) {
         std::lock_guard<std::mutex> guard(this->owning);
         return this->MemoryContext::NewPrivatedUnmanaged(size);
      }
      ObjectHeader NewPrivatedManaged(size_t size) {
         std::lock_guard<std::mutex> guard(this->owning);
         return this->MemoryContext::NewPrivatedManaged(size);
      }
      ObjectHeader NewSharedUnmanaged(size_t size) {
         std::lock_guard<std::mutex> guard(this->owning);
         return this->MemoryContext::NewSharedUnmanaged(size);
      }
      ObjectHeader NewSharedManaged(size_t size) {
         std::lock_guard<std::mutex> guard(this->owning);
         return this->MemoryContext::NewSharedManaged(size);
      }
      void FreeObject(address_t address) {
         std::lock_guard<std::mutex> guard(this->owning);
         return this->MemoryContext::FreeObject(address);
      }
   protected:
      friend struct Descriptor;
      friend struct MemoryController;
   };

   struct MemoryCentralContext {
      ObjectCentralContext unmanaged;
      ObjectCentralContext managed;
      ObjectAllocOptions options;

      void Initiate();
      void CheckValidity();
      void ForeachObjectRegion(std::function<bool(ObjectRegion)>&& visitor);

      void PerformMemoryCleanup();
   };

   struct ThreadDedicatedContext {
      bool disposable;
      ThreadDedicatedContext(MemoryContext* context = 0, bool disposable = false);
      ~ThreadDedicatedContext();
      MemoryContext& operator *();
      MemoryContext* operator ->();
      void Put(MemoryContext* context = 0, bool disposable = false);
      MemoryContext* Pop();
   };

}
