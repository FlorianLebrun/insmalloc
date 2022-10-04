#include <ins/memory/space.h>

using namespace ins;

static sArenaDescriptor UnusedArena(cst::ArenaSizeL2);
static sArenaDescriptor ForbiddenArena(cst::ArenaSizeL2);
static std::mutex arenas_lock;
MemorySpace::tMemoryState MemorySpace::state;

MemorySpace::tMemoryState::tMemoryState() {
   if (this != &MemorySpace::state) {
      throw std::exception("singleton");
   }
   for (int i = 0; i < cst::ArenaPerSpace; i++) {
      this->table[i] = ArenaEntry(&UnusedArena);
   }
   this->descriptorHeap = sDescriptorHeap::New(0);
}

void MemorySpace::SetRegionEntry(address_t address, RegionEntry entry) {
   auto arena = state.table[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   arena.descriptor()->regions[regionID] = entry;
}

RegionEntry MemorySpace::GetRegionEntry(address_t address) {
   auto arena = state.table[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   return arena.descriptor()->regions[regionID];
}

Descriptor MemorySpace::GetRegionDescriptor(address_t address) {
   auto arena = state.table[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   auto regionEntry = arena.descriptor()->regions[regionID];
   if (!regionEntry.IsFree()) {
      address.position = regionID << arena.segmentation;
      return Descriptor(address.ptr);
   }
   return 0;
}

address_t MemorySpace::ReserveArena() {
   return OSMemory::ReserveMemory(0, cst::SpaceSize, cst::ArenaSize, cst::ArenaSize);
}

address_t MemorySpace::AllocateRegion(uint32_t sizing) {
   std::lock_guard<std::mutex> guard(state.lock);
   auto& bucket = state.arenas[sizing];

   // Acquire a arena with availables regions
   auto arena = bucket.availables;
   if (!arena) {
      address_t base = MemorySpace::ReserveArena();
      if (!base) throw std::exception("OOM");
      arena = state.descriptorHeap->NewBuffer<sArenaDescriptor>(sArenaDescriptor::GetSize(sizing), sizing);
      arena->base = base;
      arena->next = bucket.availables;
      state.table[base.arenaID] = arena;
      bucket.availables = arena;
   }

   // Remove arena from availables list
   if (0 == --arena->availables_count) {
      bucket.availables = arena->next;
      arena->next = 0;
   }

   // Find free region in arena 
   auto size = size_t(1) << sizing;
   for (int i = 0; i < (cst::ArenaSize >> sizing); i++) {
      auto& tag = arena->regions[i];
      if (tag.IsFree()) {
         address_t ptr = arena->base + i * size;
         OSMemory::CommitMemory(ptr, size);
         tag = RegionEntry::cOpaqueBits;
         return ptr;
      }
   }
   throw std::exception("crash");
}

void MemorySpace::DisposeRegion(address_t address) {
   auto arena = state.table[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   auto regionSize = size_t(1) << arena.segmentation;
   auto regionOffset = address.position & (regionSize - 1);
   auto& regionEntry = arena.descriptor()->regions[regionID];
   if (regionOffset == 0 && !regionEntry.IsFree()) {
      OSMemory::DecommitMemory(address.ptr, regionSize);
      regionEntry = RegionEntry::cFreeBits;
   }
   else {
      throw std::exception("Mis aligned");
   }
}

void MemorySpace::ForeachRegion(std::function<bool(ArenaDescriptor arena, RegionEntry entry, address_t addr)>&& visitor) {
   for (address_t addr; addr.arenaID < cst::ArenaPerSpace; addr.arenaID++) {
      auto arena = state.table[addr.arenaID].descriptor();
      auto region_size = size_t(1) << arena->sizing;
      auto region_count = arena->GetRegionCount();
      addr.position = 0;
      for (size_t regionID = 0; regionID < region_count; regionID++) {
         auto& region_entry = arena->regions[regionID];
         if (!region_entry.IsFree()) {
            if (!visitor(arena, region_entry, addr)) return;
         }
         addr.position += region_size;
      }
   }
}

ObjectHeader MemorySpace::GetObject(address_t address) {
   auto arena = state.table[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   auto regionTag = arena.descriptor()->regions[regionID];
   if (!regionTag.hasNoObjects) {
      auto regionMask = (size_t(1) << arena.segmentation) - 1;
      auto region = (sObjectRegion*)(uintptr_t(address.ptr) & regionMask);
      return 0;
   }
   return 0;
}

void MemorySpace::ForeachObjectRegion(std::function<bool(ObjectRegion)>&& visitor) {
   MemorySpace::ForeachRegion(
      [&](ArenaDescriptor arena, RegionEntry entry, address_t addr) {
         if (entry.IsObjectRegion()) {
            return visitor(ObjectRegion(addr.ptr));
         }
         return true;
      }
   );
}

void MemorySpace::tMemoryState::ScheduleHeapMaintenance(ins::ObjectClassHeap* heap) {
}

void MemorySpace::tMemoryState::RunController() {

}

void MemorySpace::Print() {
   MemorySpace::ForeachRegion(
      [&](ArenaDescriptor arena, RegionEntry entry, address_t addr) {
         auto region_size = size_t(1) << arena->sizing;
         printf("\n%X%.8X %lld bytes", addr.arenaID, addr.position, region_size);
         if (!entry.hasNoObjects) {
            auto region = ObjectRegion(addr.ptr);
            auto nobj = region->objects_bin.length() + region->shared_bin.length();
            auto nobj_max = region->infos.object_count;
            printf(": layout(%d) objects(%d/%d)", region->layout, nobj, nobj_max);
            printf(" owner(%p)", region->owner);
            if (region->IsDisposable()) printf(" [empty]");
         }
         else if (entry.hasDescriptor) {
            auto region = Descriptor(addr.ptr);
            printf(": classname(%s)", typeid(region).name());
         }
         else {
            printf(": opaque");
         }
         return true;
      }
   );
   printf("\n");
}

