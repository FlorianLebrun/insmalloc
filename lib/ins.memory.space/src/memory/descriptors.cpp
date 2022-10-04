#include <ins/memory/descriptors.h>
#include <ins/memory/space.h>

using namespace ins;

void ins::sDescriptor::Dispose() {
   auto region = MemorySpace::GetRegionDescriptor(this);
   if (auto descriptors = dynamic_cast<sDescriptorHeap*>(region)) {
      descriptors->Dispose(this);
   }
   else {
      MemorySpace::DisposeRegion(this);
   }
}

void ins::sDescriptor::Resize(size_t newSize) {
   auto region = (sDescriptorHeap*)MemorySpace::GetRegionDescriptor(this);
   region->Extends(this, newSize);
}

ins::sDescriptor::~sDescriptor() {
   if (auto region = (sDescriptorHeap*)MemorySpace::GetRegionDescriptor(this)) {
      printf("fatal: destructor not allowed due to VMT rewrite");
      exit(3);
   }
}

sDescriptorHeap* ins::sDescriptorHeap::New(size_t countL2) {
   size_t count = size_t(1) << countL2;
   address_t buffer = MemorySpace::ReserveArena();
   OSMemory::CommitMemory(buffer, cst::PageSize);
   auto region = new((void*)buffer) sDescriptorHeap(countL2);
   MemorySpace::state.table[buffer.arenaID] = ArenaEntry(&region->arena);
   MemorySpace::SetRegionEntry(region, RegionEntry::cDescriptorBits);
   return region;
}

ins::sArenaDescriptor::sArenaDescriptor(uint8_t sizing) {
   this->sizing = sizing;
   this->availables_count = cst::ArenaSize >> sizing;
   memset(this->regions,RegionEntry::cFreeBits, this->availables_count);
}
