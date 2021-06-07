#include "./heaps.h"
#include "../base.h"
#include <sat/timing.hpp>

using namespace sat;

BaseHeap::BaseHeap() {
   memset(this->slots, 0, sizeof(this->slots));
}
BaseHeap::~BaseHeap() {
}

int BaseHeap::getID() {
   return this->heapID;
}

size_t BaseHeap::getMaxAllocatedSize() {
   return this->sizeMapping.getMaxAllocator()->getMaxAllocatedSize();
}
size_t BaseHeap::getMinAllocatedSize() {
   return 0;
}
size_t BaseHeap::getAllocatedSize(size_t size) {
   return this->sizeMapping.getAllocator(size)->getAllocatedSize(size);
}
size_t BaseHeap::getMaxAllocatedSizeWithMeta() {
   return this->sizeMappingWithMeta.getMaxAllocator()->getMaxAllocatedSize();
}
size_t BaseHeap::getMinAllocatedSizeWithMeta() {
   return 0;
}
size_t BaseHeap::getAllocatedSizeWithMeta(size_t size) {
   return this->sizeMappingWithMeta.getAllocator(size)->getAllocatedSizeWithMeta(size);
}

void* BaseHeap::getSlot(uintptr_t slotId) {
   return this->slots[slotId];
}

void BaseHeap::setSlot(uintptr_t slotId, void* value) {
   this->slots[slotId] = value;
}

void* BaseHeap::allocate(size_t size) {
   void* ptr;
   if (this->useOverflowProtection) {
      ptr = this->allocateWithMeta(size, 0);
   }
   else {
      sat::ObjectAllocator* allocator = this->sizeMapping.getAllocator(size);
      ptr = allocator->allocate(size);
   }

#ifdef _DEBUG
   SAT_DEBUG_CHECK(sat::tObjectInfos infos);
   SAT_DEBUG_CHECK(bool hasInfos = sat_get_address_infos(ptr, &infos));
   SAT_DEBUG_CHECK(assert(hasInfos));
   SAT_DEBUG_CHECK(assert(infos.base == uintptr_t(ptr)));
   SAT_DEBUG_CHECK(assert(infos.size >= size));
#endif
   return ptr;
}

void* BaseHeap::allocateWithMeta(size_t size, uint64_t meta) {
   void* ptr;
   if (this->useOverflowProtection) {
      static const size_t paddingMinSize = 8;
      size_t allocatedSize = size + paddingMinSize + sizeof(tObjectStamp);
      meta |= tObjectMetaData::cIsOverflowProtected_Bit | tObjectMetaData::cIsStamped_Bit;

      // Allocate buffer
      sat::ObjectAllocator* allocator = this->sizeMappingWithMeta.getAllocator(allocatedSize);
      ptr = allocator->allocateWithMeta(allocatedSize, meta);

      // Prepare buffer tail
      {
         allocatedSize = allocator->getAllocatedSizeWithMeta(allocatedSize);

         // Write stamp
         tpObjectStamp stamp = tpObjectStamp(((uint8_t*)ptr) + allocatedSize - sizeof(tObjectStamp));
         stamp->stackstamp = 0;// Thread::current()->getStackStamp();
         stamp->timestamp = sat::timing::getCurrentTimestamp();

         // Write overflow detection bytes
         uint32_t& bufferSize = *(uint32_t*)(((uint8_t*)ptr) + allocatedSize - sizeof(uint32_t) - sizeof(tObjectStamp));
         bufferSize = size ^ 0xabababab;
         uint8_t* paddingBytes = ((uint8_t*)ptr) + size;
         memset(paddingBytes, 0xab, allocatedSize - size - sizeof(uint32_t) - sizeof(tObjectStamp));
      }
   }
   else {
      sat::ObjectAllocator* allocator = this->sizeMappingWithMeta.getAllocator(size);
      ptr = allocator->allocateWithMeta(size, meta);
   }

#ifdef _DEBUG
   SAT_DEBUG_CHECK(sat::tObjectInfos infos);
   SAT_DEBUG_CHECK(bool hasInfos = sat_get_address_infos(ptr, &infos));
   SAT_DEBUG_CHECK(assert(hasInfos));
   SAT_DEBUG_CHECK(assert(infos.base == uintptr_t(ptr)));
   SAT_DEBUG_CHECK(assert(infos.size >= size));
   SAT_DEBUG_CHECK(assert(infos.meta.bits == meta));
#endif
   return ptr;
}

GlobalHeap::GlobalHeap(const char* name, int heapID) {
   this->heapID = heapID;
   this->locals_list = 0;
   strcpy_s(this->name, 256, name ? name : "");
}

GlobalHeap::~GlobalHeap() {
   sat::heaps_lock.lock();
   assert(sat::heaps_table[this->heapID] == this);
   sat::heaps_table[this->heapID] = 0;
   if (this->nextGlobal) this->nextGlobal->backGlobal = this->backGlobal;
   if (this->backGlobal) this->backGlobal->nextGlobal = this->nextGlobal;
   else sat::heaps_list = this->nextGlobal;
   sat::heaps_lock.unlock();
}

const char* GlobalHeap::getName() {
   return this->name;
}

uintptr_t GlobalHeap::acquireSlot(void* value)  { 
   uintptr_t slotID = this->slotsCount++;
   this->setSlot(slotID, value);
   return slotID;
}

uintptr_t GlobalHeap::acquirePages(size_t size) {
   return sat::MemoryTableController::self.allocSegmentSpan(size);
}

void GlobalHeap::releasePages(uintptr_t index, size_t size) {
   return sat::MemoryTableController::self.freeSegmentSpan(index, size);
}

LocalHeap::LocalHeap(GlobalHeap* global) {
   global->retain();
   global->locals_lock.lock();
   this->heapID = global->heapID;
   this->global = global;
   if (this->nextLocal = global->locals_list) global->locals_list->backLocal = this;
   this->backLocal = 0;
   global->locals_list = this;
   global->locals_lock.unlock();
}

LocalHeap::~LocalHeap() {
   this->global->locals_lock.lock();
   if (this->nextLocal) this->nextLocal->backLocal = this->backLocal;
   if (this->backLocal) this->backLocal->nextLocal = this->nextLocal;
   else this->global->locals_list = this->nextLocal;
   this->global->locals_lock.unlock();
}

const char* LocalHeap::getName() {
   return this->global->name;
}

uintptr_t LocalHeap::acquirePages(size_t size) {
   return sat::MemoryTableController::self.allocSegmentSpan(size);
}

void LocalHeap::releasePages(uintptr_t index, size_t size) {
   return sat::MemoryTableController::self.freeSegmentSpan(index, size);
}

