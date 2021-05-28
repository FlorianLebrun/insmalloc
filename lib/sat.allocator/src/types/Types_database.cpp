#include "./Types_database.hpp"
#include "../allocators/ZonedBuddy/index.h"

using namespace sat;

uintptr_t sat_types_base = 0;

struct TypesDataHeap : public SystemObject<Heap>::Derived<TypesDataHeap>, public MemorySegmentController {
  ZonedBuddyAllocator::Global::Cache l_typesGlobal;
  ZonedBuddyAllocator::Local::Cache l_typesLocal;

  uintptr_t index;
  uintptr_t length;
  uintptr_t reserved;
  SpinLock heaplock;
  SpinLock pagelock;

  std::atomic_intptr_t memoryUse;

  void init() {
    this->length = 0;
    this->reserved = 4096;
    this->index = sat::MemoryTableController::self.reserveMemory(4096, 4096);
    for (uintptr_t i = 0; i < this->reserved; i++) {
      sat::MemoryTableController::set(this->index + i, this);
    }

    this->l_typesGlobal.init(&g_typesDataheap);
    this->l_typesLocal.init(this, &this->l_typesGlobal);

    sat_types_base = this->index << sat::memory::cSegmentSizeL2;
  }
  virtual int getID() override {
    return -1;
  }
  virtual const char* getName() override {
    return "Types database heap";
  }
  virtual size_t getMaxAllocatedSize() override {
    return this->reserved << sat::memory::cSegmentSizeL2;
  }
  virtual size_t getMinAllocatedSize() override {
    return 0;
  }
  virtual size_t getAllocatedSize(size_t size) override {
    return size;
  }
  virtual size_t getMaxAllocatedSizeWithMeta() override {
    return this->getMaxAllocatedSize();
  }
  virtual size_t getMinAllocatedSizeWithMeta() override {
    return this->getMinAllocatedSize();
  }
  virtual size_t getAllocatedSizeWithMeta(size_t size) override {
    return this->getAllocatedSize(size);
  }
  virtual void* allocate(size_t size) override {
    this->heaplock.lock();
    sat::ObjectAllocator* allocator = g_typesDataheap.l_typesLocal.getSizeAllocator(size);
    void* ptr = allocator->allocate(size);
    this->heaplock.unlock();
    return ptr;
  }
  virtual void* allocateWithMeta(size_t size, uint64_t meta) override {
    this->heaplock.lock();
    sat::ObjectAllocator* allocator = g_typesDataheap.l_typesLocal.getSizeAllocator(size);
    void* ptr = allocator->allocateWithMeta(size, meta);
    this->heaplock.unlock();
    return ptr;
  }

  virtual size_t getSize() {
    return this->memoryUse;
  }
  virtual uintptr_t acquireSlot(void* value) { return 0; }
  virtual void* getSlot(uintptr_t slotId) { return 0; }
  virtual void setSlot(uintptr_t slotId, void* value) { return ; }
  virtual uintptr_t acquirePages(size_t size) override
  {
    uintptr_t index = 0;

    // Alloc a page
    this->pagelock.lock();
    if (length < reserved) {
      index = this->index + (this->length++);
    }
    this->pagelock.unlock();

    // Initialize the page
    if (index) {
      this->memoryUse += sat::memory::cSegmentSize;
      sat::MemoryTableController::self.commitMemory(index, 1);
      return index;
    }
    return 0;
  }
  virtual void releasePages(uintptr_t index, size_t size) override
  {
    printf("TypesDataBase memory leak");
  }
  virtual void destroy() {

  }
} g_typesDataheap;

static struct TypesDataBase : sat::TypesController {

  virtual TypeDef getType(TypeDefID typeID) override;
  virtual TypeDefID getTypeID(TypeDef typ) override;

  virtual TypeDef allocTypeDef(ITypeHandle* handle, uint32_t nrefs) override;

  virtual void* alloc(int size);
  virtual void free(void* ptr);

} g_typesDatabase;

void sat::TypesDataBase_init() {
  g_typesDataheap.init();
}

sat::TypesController* sat_get_types_controller() {
  return &g_typesDatabase;
}

void* TypesDataBase::alloc(int size) {
  return g_typesDataheap.allocate(size);
}

void TypesDataBase::free(void* ptr) {
}

TypeDef TypesDataBase::getType(TypeDefID typeID) {
  uintptr_t pageIndex = typeID>>sat::memory::cSegmentSizeL2;
  if(typeID > 0 && pageIndex < g_typesDataheap.length) {
    return TypeDef(typeID + ::sat_types_base);
  }
  return 0;
}

TypeDefID TypesDataBase::getTypeID(TypeDef typ) {
  return uintptr_t(typ) - sat_types_base;
}

TypeDef TypesDataBase::allocTypeDef(ITypeHandle* handle, uint32_t nrefs) {
   int length = sizeof(tTypeDef) + nrefs * sizeof(uint32_t);
  assert(length >= sizeof(tTypeDef) && length < 16834);
  TypeDef typ = (TypeDef)TypesDataBase::alloc(length);
  memset(typ, 0, length);
  typ->length = length;
  typ->handle = handle;
  typ->nrefs = nrefs;
  memset(typ->refs, 0, nrefs * sizeof(uint32_t));
  return typ;
}

