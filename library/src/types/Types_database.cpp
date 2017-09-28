#include "../base.h"
#include "../allocator-ZonedBuddy/index.h"

using namespace SAT;

uintptr_t sat_types_base = 0;

struct TypesDataHeap : SATBasicRealeasable<IHeap> {
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
    this->index = g_SAT.reserveMemory(4096, 4096);
    for (uintptr_t i = 0; i < this->reserved; i++) {
      g_SATable[this->index + i].id = SAT::tEntryID::TYPES_DATABASE;
    }

    this->l_typesGlobal.init(&g_typesDataheap, SAT::tEntryID::TYPES_DATABASE);
    this->l_typesLocal.init(&this->l_typesGlobal);

    sat_types_base = this->index << SAT::cSegmentSizeL2;
  }
  virtual int getID() override {
    return -1;
  }
  virtual const char* getName() override {
    return "Types database heap";
  }

  virtual void* allocBuffer(size_t size, SAT::tObjectMetaData meta) override {
    this->heaplock.lock();
    IObjectAllocator* allocator = g_typesDataheap.l_typesLocal.getSizeAllocator(size);
    void* ptr = allocator->allocObject(size, meta.bits);
    this->heaplock.unlock();
    return ptr;
  }

  virtual size_t getSize() {
    return this->memoryUse;
  }
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
      this->memoryUse += SAT::cSegmentSize;
      g_SAT.commitMemory(index, 1);
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

static struct TypesDataBase : SAT::ITypesController {
  
  virtual TypeDef getType(TypeDefID typeID) override;
  virtual TypeDefID getTypeID(TypeDef typ) override;

  virtual TypeDef allocTypeDef(uint32_t length) override;

  virtual void* alloc(int size);
  virtual void free(void* ptr);

} g_typesDatabase;

void SAT::TypesDataBase_init() {
  g_typesDataheap.init();
}

SAT::ITypesController* sat_get_types_controller() {
  return &g_typesDatabase;
}

void* TypesDataBase::alloc(int size) {
  return g_typesDataheap.allocBuffer(size, 0);
}

void TypesDataBase::free(void* ptr) {
}


TypeDef TypesDataBase::getType(TypeDefID typeID)
{
  uintptr_t pageIndex = typeID>>SAT::cSegmentSizeL2;
  if(typeID > 0 && pageIndex < g_typesDataheap.length) {
    return TypeDef(typeID + ::sat_types_base);
  }
  return 0;
}

TypeDefID TypesDataBase::getTypeID(TypeDef typ)
{
  return uintptr_t(typ) - sat_types_base;
}

TypeDef TypesDataBase::allocTypeDef(uint32_t length) {
  assert(length >= sizeof(tTypeDef) && length < 16834);
  TypeDef typ = (TypeDef)TypesDataBase::alloc(length);
  memset(typ, 0, length);
  typ->length = length;
  return typ;
}