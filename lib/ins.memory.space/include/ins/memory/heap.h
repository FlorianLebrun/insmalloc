#pragma once
#include <ins/memory/space.h>

namespace ins {

   struct MemoryHeap;
   struct MemoryContext;

   struct MemoryContext {
      MemoryHeap* heap;
      ObjectClassContext objects[ins::ObjectLayoutCount];

      void Clean();
      void CheckValidity();

      void FreeObject(ObjectHeader obj);

   private:
      friend struct sDescriptorHeap;
      friend struct MemoryHeap;
      MemoryContext(MemoryHeap* heap);
   };

   struct MemoryHeap {

      struct sContext : sDescriptor, MemoryContext {
         sContext* next = 0;
         bool allocated = false;
         sContext(MemoryHeap* heap);
         size_t GetSize() override;
      };

      ObjectClassHeap objects[ins::ObjectLayoutCount];
      sContext* contexts = 0;
      std::mutex contexts_lock;

      MemoryHeap();
      ~MemoryHeap();
      MemoryContext* AcquireContext();
      void DisposeContext(MemoryContext*);
      void Clean();
      void CheckValidity();
   };

   inline void MemoryContext::FreeObject(ObjectHeader obj) {
      auto region = (ObjectRegion)MemorySpace::GetRegionDescriptor(obj);
      this->objects[region->layout].FreeObject(obj, region);
   }
}
