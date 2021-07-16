#include "../../allocators/ZonedBuddy/index.h"
#include "../../allocators/LargeObject/index.h"

using namespace sat;

struct GlobalCompactHeap : public SystemObject<sat::GlobalHeap>::Derived<GlobalCompactHeap> {
  LargeObjectAllocator::Global largeObject;
  ZonedBuddyAllocator::Global::Cache zonedBuddy;

  GlobalCompactHeap(const char* name, int heapID) : Derived(name, heapID) {
    
    // Init allocators
    this->zonedBuddy.init(this);
    this->largeObject.init(this);

    // Register zonedBuddy allocators
    for (int baseID = 0; baseID <= 6; baseID++) {
      sat::ObjectAllocator* base = this->zonedBuddy.getAllocator(baseID << 4);
      this->sizeMapping.registerAllocator(base);
      this->sizeMappingWithMeta.registerAllocator(base);
      for (int dividor = 8; dividor <= 16; dividor++) {
        sat::ObjectAllocator* zoned = this->zonedBuddy.getAllocator((baseID << 4) | dividor);
        this->sizeMapping.registerAllocator(zoned);
        this->sizeMappingWithMeta.registerAllocator(zoned);
      }
    }

    // Register largeObject allocator
    this->sizeMapping.registerAllocator(&this->largeObject);
    this->sizeMappingWithMeta.registerAllocator(&this->largeObject);

    // Compute size mappings
    this->sizeMapping.finalize();
    this->sizeMappingWithMeta.finalize();
  }
  virtual ~GlobalCompactHeap() override {
     this->zonedBuddy.flushCache();
  }
  virtual void flushCache() override
  {
    this->zonedBuddy.flushCache();
  }
  virtual sat::LocalHeap* createLocal();
};

struct LocalCompactHeap : SystemObject<sat::LocalHeap>::Derived<LocalCompactHeap> {
  ZonedBuddyAllocator::Local::Cache zonedBuddy;

  LocalCompactHeap(GlobalCompactHeap* global) : Derived(global) {
      
    // Init allocators
    this->zonedBuddy.init(this, &global->zonedBuddy);

    // Register zonedBuddy allocators
    for (int baseID = 0; baseID <= 6; baseID++) {
      sat::ObjectAllocator* base = this->zonedBuddy.getAllocator(baseID << 4);
      this->sizeMapping.registerAllocator(base);
      this->sizeMappingWithMeta.registerAllocator(base);
      for (int dividor = 8; dividor <= 16; dividor++) {
        sat::ObjectAllocator* zoned = this->zonedBuddy.getAllocator((baseID << 4) | dividor);
        this->sizeMapping.registerAllocator(zoned);
        this->sizeMappingWithMeta.registerAllocator(zoned);
      }
    }

    // Register largeObject allocator
    this->sizeMapping.registerAllocator(&global->largeObject);
    this->sizeMappingWithMeta.registerAllocator(&global->largeObject);

    // Compute size mappings
    this->sizeMapping.finalize();
    this->sizeMappingWithMeta.finalize();
  }
  virtual ~LocalCompactHeap() override {
  }
  virtual void flushCache() override
  {
    this->zonedBuddy.flushCache();
  }
};

sat::LocalHeap* GlobalCompactHeap::createLocal() {
   LocalCompactHeap* heap = new LocalCompactHeap(this);
  return heap;
}

namespace sat {
  sat::GlobalHeap* createHeapCompact(const char* name) {
    return new GlobalCompactHeap(name, 0);
  }
}
