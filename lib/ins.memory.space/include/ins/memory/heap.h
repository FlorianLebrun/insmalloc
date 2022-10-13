#pragma once
#include <ins/memory/space.h>
#include <ins/memory/schemas.h>

namespace ins {

   struct MemoryHeap;
   struct MemoryContext;
   struct MemorySharedContext;

   union MemoryAllocOptions {
      struct {
         uint8_t enableTimeStamp : 1;
         uint8_t enableStackStamp : 1;
         uint8_t enableSecurityPadding;
      };
      struct {
         uint8_t enableAnalytics : 2;
      };
      uint16_t enableds = 0;
   };
   
   struct MemoryContext : protected Descriptor {
   protected:
      MemoryContext* next = 0;
      bool allocated = false;
   public:
      MemoryHeap* heap;
      MemoryAllocOptions options;

      SlabbedObjectContext objects_slabbed[ins::ObjectLayoutCount];
      LargeObjectContext objects_large[ins::ObjectLayoutCount];
      UncachedLargeObjectContext objects_uncached;

      std::mutex owning;

      address_t AllocateBuffer(size_t size);
      void FreeBuffer(address_t ptr);

      ObjectHeader AllocateObject(size_t size);
      ObjectHeader AllocateInstrumentedObject(size_t size);
      void FreeObject(ObjectHeader obj);

      void Clean();
      void CheckValidity();

   protected:
      friend struct Descriptor;
      friend struct MemoryHeap;
      void Initiate(MemoryHeap* heap);
      size_t GetSize() override;
   };

   struct MemorySharedContext : protected MemoryContext {
      std::mutex lock;
      void Clean() {
         std::lock_guard<std::mutex> guard(lock);
         return this->MemoryContext::Clean();
      }
      void CheckValidity() {
         std::lock_guard<std::mutex> guard(lock);
         return this->MemoryContext::CheckValidity();
      }
      address_t AllocateBuffer(size_t size) {
         std::lock_guard<std::mutex> guard(lock);
         return this->MemoryContext::AllocateBuffer(size);
      }
      void FreeBuffer(address_t ptr) {
         std::lock_guard<std::mutex> guard(lock);
         return this->MemoryContext::FreeBuffer(ptr);
      }
      ObjectHeader AllocateObject(size_t size) {
         std::lock_guard<std::mutex> guard(lock);
         return this->MemoryContext::AllocateObject(size);
      }
      void FreeObject(ObjectHeader obj) {
         std::lock_guard<std::mutex> guard(lock);
         return this->MemoryContext::FreeObject(obj);
      }
   protected:
      friend struct Descriptor;
      friend struct MemoryHeap;
      size_t GetSize() override {
         return sizeof(*this);
      }
   };

   struct MemoryHeap : Descriptor {

      SlabbedObjectHeap objects_slabbed[ins::ObjectLayoutCount];
      LargeObjectHeap objects_large[ins::ObjectLayoutCount];
      UncachedLargeObjectHeap objects_uncached;

      MemorySharedContext* defaultContext;
      MemoryAllocOptions options;
      MemoryContext* contexts = 0;
      std::mutex contexts_lock;

      MemoryHeap();
      ~MemoryHeap();
      size_t GetSize() override;
      MemoryContext* AcquireContext();
      void DisposeContext(MemoryContext*);
      void Clean();
      void CheckValidity();

      void SetTimeStampOption(bool enabled);
      void SetStackStampOption(bool enabled);
      void SetSecurityPaddingOption(uint32_t paddingSize);
   };

   struct MemoryContextOwner {
      MemoryContextOwner(MemoryContext* context);
      ~MemoryContextOwner();
   };

}

typedef void* (*tp_ins_malloc)(size_t size);
typedef void* (*tp_ins_realloc)(void* ptr, size_t size);
typedef size_t(*tp_ins_msize)(void* ptr);
typedef void (*tp_ins_free)(void*);

extern"C" ins::MemoryHeap & ins_get_heap();
extern"C" ins::MemoryContext* ins_get_thread_context();
extern"C" bool ins_check_overflow(void* ptr);
extern"C" bool ins_get_metadata(void* ptr, ins::ObjectAnalyticsInfos & meta);
extern"C" size_t ins_msize(void* ptr, tp_ins_msize default_msize = 0);

extern"C" void* ins_malloc(size_t size);
extern"C" void* ins_calloc(size_t count, size_t size);
extern"C" void* sat_realloc(void* ptr, size_t size, tp_ins_realloc default_realloc = 0);
extern"C" void ins_free(void* ptr);

extern"C" ins::ObjectHeader ins_malloc_ex(size_t size);
extern"C" ins::ObjectHeader ins_malloc_schema(ins::SchemaID schemaID);
extern"C" void ins_free_ex(ins::ObjectHeader obj);
