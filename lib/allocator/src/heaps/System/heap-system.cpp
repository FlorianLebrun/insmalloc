#include "../../allocators/Buddy/index.h"

using namespace sat;

struct GlobalSystemHeap : public SystemObject<sat::GlobalHeap>::Derived<GlobalSystemHeap> {
  BuddyAllocator::Global buddyAllocator;

  GlobalSystemHeap(const char* name, int heapID) : Derived(name, heapID) {
    
    // Init allocators
    this->buddyAllocator.init(this);

    // Compute size mappings
    this->sizeMapping.finalize();
    this->sizeMappingWithMeta.finalize();
  }
  virtual ~GlobalSystemHeap() override {
  }
  virtual void flushCache() override
  {
  }
  virtual sat::LocalHeap* createLocal();
};

sat::LocalHeap* GlobalSystemHeap::createLocal() {
   throw;
  //return this;
}

namespace sat {
  sat::GlobalHeap* createHeapSystem(const char* name) {
    return new GlobalSystemHeap(name, 0);
  }
}
