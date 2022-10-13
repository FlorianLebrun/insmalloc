#include <ins/memory/objects-large.h>
#include <ins/memory/space.h>

using namespace ins;

sLargeObjectRegion::sLargeObjectRegion(const tObjectLayoutInfos& infos)
   : sObjectRegion(infos) {
}

size_t sLargeObjectRegion::GetSize() {
   return MemorySpace::GetRegionSize(this);
}

void sLargeObjectRegion::DisplayToConsole() {
   address_t addr(this);
   auto region_size = size_t(1) << this->infos.region_sizeL2;
   printf("\n%X%.8X %lld bytes:", addr.arenaID, addr.position, region_size);
   printf(" owner(%p)", this->owner);
}


/**********************************************************************
*
*   Large Object Class Heap
*
***********************************************************************/

void LargeObjectHeap::Initiate(uint8_t layout) {
   this->layout = layout;
}

void LargeObjectHeap::Clean() {
}

/**********************************************************************
*
*   Large Object Class Context
*
***********************************************************************/

void LargeObjectContext::Initiate(LargeObjectHeap* heap) {
   this->heap = heap;
   this->layout = layout;
}

void LargeObjectContext::Clean() {
}

void LargeObjectContext::CheckValidity() {
}

ObjectHeader LargeObjectContext::AllocatePrivateObject(size_t size) {
   return 0;
}

ObjectHeader LargeObjectContext::AllocateSharedObject(size_t size) {
   return 0;
}

void LargeObjectContext::FreeObject(LargeObjectRegion region) {

}

/**********************************************************************
*
*   Uncached Large Object Heap + Context
*
***********************************************************************/
void UncachedLargeObjectProvider::NotifyAvailableRegion(sObjectRegion* region) {
   MemorySpace::DisposeRegion(region);
}

void UncachedLargeObjectHeap::Initiate(uint8_t layout) {
   this->layout = layout;
}

LargeObjectRegion UncachedLargeObjectHeap::AllocateObjectRegion(size_t size) {
   auto allocatedSize = ins::align<65536>(size + sizeof(sLargeObjectRegion));
   auto regionSizeL2 = ins::msb_64(allocatedSize);
   auto regionAddr = MemorySpace::AllocateRegion(regionSizeL2);
   return new (regionAddr) sLargeObjectRegion(ObjectLayoutInfos[this->layout]);
}

ObjectHeader UncachedLargeObjectHeap::AllocateObject(size_t size) {
   auto region = this->AllocateObjectRegion(size);
   region->owner = this;
   return &region->header;
}

void UncachedLargeObjectContext::Initiate(UncachedLargeObjectHeap* heap) {
   this->heap = heap;
   this->layout = layout;
}

ObjectHeader UncachedLargeObjectContext::AllocatePrivateObject(size_t size) {
   auto region = this->heap->AllocateObjectRegion(size);
   region->owner = this;
   return &region->header;
}

ObjectHeader UncachedLargeObjectContext::AllocateSharedObject(size_t size) {
   return this->heap->AllocateObject(size);
}
