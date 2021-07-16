#include "./index.h"

using namespace BuddyAllocator;

const char* BuddySegmentController::getName() {
   return "Buddy";
}

int BuddySegmentController::free(uintptr_t index, uintptr_t ptr) {
   auto obj = (BuddyFreeObject*)ptr;
   obj->next = 0;
   return 0;
}

bool BuddySegmentController::getAddressInfos(uintptr_t index, uintptr_t ptr, sat::tpObjectInfos infos) {
   return false;
}

int BuddySegmentController::traverseObjects(uintptr_t index, sat::IObjectVisitor* visitor) {
   return 0;
}


void BuddyAllocator::Global::init(sat::Heap* heap) {
   this->controller.heapID = heap->getID();
}


size_t BuddyAllocator::Global::getMaxAllocatedSize() { return 0; }
size_t BuddyAllocator::Global::getMinAllocatedSize() { return 0; }
size_t BuddyAllocator::Global::getAllocatedSize(size_t size) { return 0; }
void* BuddyAllocator::Global::allocate(size_t size) { return 0; }

size_t BuddyAllocator::Global::getMaxAllocatedSizeWithMeta() { return 0; }
size_t BuddyAllocator::Global::getMinAllocatedSizeWithMeta() { return 0; }
size_t BuddyAllocator::Global::getAllocatedSizeWithMeta(size_t size) { return 0; }
void* BuddyAllocator::Global::allocateWithMeta(size_t size, uint64_t meta) { return 0; }
