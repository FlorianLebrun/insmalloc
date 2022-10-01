#include <ins/memory/descriptors.h>
#include <ins/memory/space.h>

using namespace ins;

void ins::sDescriptor::Dispose() {
   auto region = (sDescriptorAllocator*)space.GetRegionDescriptor(this);
   region->Dispose(this);
}

void ins::sDescriptor::Resize(size_t newSize) {
   auto region = (sDescriptorAllocator*)space.GetRegionDescriptor(this);
   region->Extends(this, newSize);
}

sDescriptorAllocator* ins::sDescriptorAllocator::New(size_t countL2) {
   size_t count = size_t(1) << countL2;
   address_t buffer = space.ReserveArena();
   OSMemory::CommitMemory(buffer, cst::PageSize);
   auto region = new((void*)buffer) sDescriptorAllocator(countL2);
   space.arenas[buffer.arenaID] = ArenaEntry(&region->arena);
   space.SetRegionEntry(region, RegionEntry::cDescriptorBits);
   return region;
}

ins::sArenaDescriptor::sArenaDescriptor(uint8_t sizing) {
   this->sizing = sizing;
   this->availables_count = cst::ArenaSize >> sizing;
   memset(this->regions,RegionEntry::cFreeBits, this->availables_count);
}
