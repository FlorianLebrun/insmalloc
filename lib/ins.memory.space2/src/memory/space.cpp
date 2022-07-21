#include <stdint.h>
#include <semaphore>
#include <stdlib.h>
#include "./objects.h"
#include "../os/memory.h"

struct MemoryContext;

namespace ins {

   struct sObjectClassContext {
      ObjectRegion usedRegion = 0;
      ObjectChain firstObject = 0;
      ObjectChain middleObject = 0;
      int numObjectNominal = 8;
      int numObjectMax = 16;
      int numObject = 0;

      ObjectHeader Allocate(MemoryContext* context);
      ObjectHeader AlllocateInBucket(MemoryContext* context);
      ObjectHeader AllocateInPage(MemoryContext* context);
      void Dispose(ObjectHeader* obj, MemoryContext* context);

   };


   const size_t c_ObjectClassMemoryCount = 1;

   struct ObjectClassSpace {
      ObjectRegion firstRegion;
      ObjectRegion currentRegion;

   };

   struct MemorySpace {
      ObjectClassSpace classes[c_ObjectClassMemoryCount];
      void RunController();
   };


   struct MemoryContext {
      MemorySpace* space;
      sObjectClassContext classes[c_ObjectClassMemoryCount];

      ObjectHeader AllocateObject(int id);
      void DisposeObject(void* ptr);
   };
}

using namespace ins;
/*
ObjectHeader ins::sObjectClassContext::Allocate(MemoryContext* context) {
   
   // Try allocate from buckets
   if (auto obj = this->firstObject) {
      this->firstObject = obj->next;
      this->numObject--;
      if (obj == this->middleObject) {
         this->middleObject = 0;
      }
      return obj;
   }
   
   return 0;
}

void sObjectClassContext::Dispose(ObjectHeader* obj, MemoryContext* context) {
   if (this->numObject) {

   }
}

ObjectHeader MemoryContext::AllocateObject(int id) {
   return this->classes[id].Allocate(this);
}

void MemoryContext::DisposeObject(void* ptr) {
   throw;
}
*/


struct DescriptorsRegion {
};

namespace cst {
   const size_t RegionSizeL2 = 20;
   const size_t RegionSize = size_t(1) << RegionSizeL2;
};

void MemorySpace::RunController() {

}