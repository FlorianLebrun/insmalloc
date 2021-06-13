#include "./index.h"

LargeObjectAllocator::LargeObjectSegment::LargeObjectSegment(uint8_t heapID, uint32_t index, uint32_t length, uint64_t meta) {
   this->heapID = heapID;
   this->index = index;
   this->length = length;
   this->meta = meta;
}

const char* LargeObjectAllocator::LargeObjectSegment::getName() {
   return "LARGE_OBJECT";
}

int LargeObjectAllocator::LargeObjectSegment::free(uintptr_t index, uintptr_t ptr) {
   volatile auto entry = this;
   assert(entry->index == 0);
   size_t length = entry->length;
   sat::MemoryTableController::self.freeSegmentSpan(index, length);
   return length << sat::memory::cSegmentSizeL2;
}

bool LargeObjectAllocator::LargeObjectSegment::getAddressInfos(uintptr_t index, uintptr_t ptr, sat::tpObjectInfos infos) {
   if (infos) {
      auto objectSize = (size_t(this->length) << sat::memory::cSegmentSizeL2) - sizeof(LargeObjectSegment);
      infos->set(this->heapID, uintptr_t(&this[1]), objectSize, &this->meta);
   }
   return true;
}

int LargeObjectAllocator::LargeObjectSegment::traverseObjects(uintptr_t index, sat::IObjectVisitor* visitor) {
   sat::tObjectInfos infos;
   auto objectSize = (size_t(this->length) << sat::memory::cSegmentSizeL2) - sizeof(LargeObjectSegment);
   infos.set(this->heapID, uintptr_t(&this[1]), objectSize, &this->meta);
   visitor->visit(&infos);
   return this->length;
}

void LargeObjectAllocator::Global::init(sat::Heap* heap) {
   this->heapID = heap->getID();
}

void* LargeObjectAllocator::Global::allocate(size_t size) {
   return this->LargeObjectAllocator::Global::allocateWithMeta(size, 0);
}

void* LargeObjectAllocator::Global::allocateWithMeta(size_t size, uint64_t meta) {

   // Allocate memory space
   uint32_t length = alignX<int32_t>(size + sizeof(LargeObjectSegment), sat::memory::cSegmentSize) >> sat::memory::cSegmentSizeL2;
   uintptr_t index = sat::MemoryTableController::self.allocSegmentSpan(length);

   // Install controller
   auto controller = new((void*)(index << sat::memory::cSegmentSizeL2)) LargeObjectSegment(this->heapID, index, length, meta);
   for (uint32_t i = 0; i < length; i++) {
      sat::MemoryTableController::table[i] = controller;
   }

   return &controller[1];
}

size_t LargeObjectAllocator::Global::getMaxAllocatedSize() {
   return sat::MemoryTableController::self.limit << sat::memory::cSegmentSizeL2;
}

size_t LargeObjectAllocator::Global::getMinAllocatedSize() {
   return sat::memory::cSegmentSize;
}

size_t LargeObjectAllocator::Global::getAllocatedSize(size_t size) {
   return alignX<int32_t>(size, sat::memory::cSegmentSize);
}

size_t LargeObjectAllocator::Global::getMaxAllocatedSizeWithMeta() {
   return this->LargeObjectAllocator::Global::getMaxAllocatedSize();
}

size_t LargeObjectAllocator::Global::getMinAllocatedSizeWithMeta() {
   return this->LargeObjectAllocator::Global::getMinAllocatedSize();
}

size_t LargeObjectAllocator::Global::getAllocatedSizeWithMeta(size_t size) {
   return this->LargeObjectAllocator::Global::getAllocatedSize(size);
}

