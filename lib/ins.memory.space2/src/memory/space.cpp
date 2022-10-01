#include <ins/memory/space.h>

using namespace ins;

static sArenaDescriptor UnusedArena(cst::ArenaSizeL2);
static sArenaDescriptor ForbiddenArena(cst::ArenaSizeL2);
static std::mutex arenas_lock;
ins::MemorySpace ins::space;

ins::MemorySpace::MemorySpace() {
   if (this != &ins::space) {
      throw std::exception("singleton");
   }
   for (int i = 0; i < cst::ArenaPerSpace; i++) {
      this->arenas[i] = ArenaEntry(&UnusedArena);
   }
   this->descriptorAllocator = sDescriptorAllocator::New(0);
}

void ins::MemorySpace::SetRegionEntry(address_t address, RegionEntry entry) {
   auto arena = arenas[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   arena.descriptor()->regions[regionID] = entry;
}

RegionEntry ins::MemorySpace::GetRegionEntry(address_t address) {
   auto arena = arenas[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   return arena.descriptor()->regions[regionID];
}

Descriptor ins::MemorySpace::GetRegionDescriptor(address_t address) {
   auto arena = arenas[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   if (!arena.descriptor()->regions[regionID].IsFree()) {
      address.position = regionID << arena.segmentation;
      return Descriptor(address.ptr);
   }
   return 0;
}

address_t ins::MemorySpace::ReserveArena() {
   return OSMemory::ReserveMemory(0, cst::SpaceSize, cst::ArenaSize, cst::ArenaSize);
}

address_t ins::MemorySpace::AllocateRegion(uint32_t sizing) {
   std::lock_guard<std::mutex> guard(this->lock);
   auto& bucket = this->regions[sizing];

   // Acquire a arena with availables regions
   auto arena = bucket.availables;
   if (!arena) {
      address_t base = this->ReserveArena();
      if (!base) throw std::exception("OOM");
      arena = space.descriptorAllocator->NewBuffer<sArenaDescriptor>(sArenaDescriptor::GetSize(sizing), sizing);
      arena->base = base;
      arena->next = bucket.availables;
      arenas[base.arenaID] = arena;
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

void ins::MemorySpace::DisposeRegion(address_t base, uint32_t sizing) {
   throw std::exception("TODO");
}

ObjectHeader ins::MemorySpace::GetObject(address_t address) {
   auto arena = arenas[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   auto regionTag = arena.descriptor()->regions[regionID];
   if (!regionTag.hasNoObjects) {
      auto regionMask = (size_t(1) << arena.segmentation) - 1;
      auto region = (sObjectRegion*)(uintptr_t(address.ptr) & regionMask);
      return 0;
   }
   return 0;
}

void ins::MemorySpace::ScheduleHeapMaintenance(ins::ObjectClassHeap* heap) {
}

void ins::MemorySpace::RunController() {
   std::thread(
      []() {

      }
   );
}

void ins::MemorySpace::Print() {
   for (address_t addr; addr.arenaID < cst::ArenaPerSpace; addr.arenaID++) {
      auto arena = arenas[addr.arenaID].descriptor();
      auto region_count = arena->GetRegionCount();
      auto region_size = size_t(1) << arena->sizing;
      addr.position = 0;
      for (size_t regionID = 0; regionID < region_count; regionID++) {
         auto& tag = arena->regions[regionID];
         if (!tag.IsFree()) {
            printf("\n[%d,%d] %lld bytes", addr.arenaID, regionID, region_size);
            if (!tag.hasNoObjects) {
               auto region = ObjectRegion(addr.ptr);
               auto nobj = region->objects_bin.length() + region->shared_bin.length();
               auto nobj_max = region->infos.object_count;
               printf(": layout(%d) objects(%d/%d)", region->layout, nobj, nobj_max);
               if (region->IsEmpty()) printf(" [empty]");
            }
            else if (tag.hasDescriptor) {
               auto region = Descriptor(addr.ptr);
               printf(": classname(%s)", typeid(region).name());
            }
            else {
               printf(": opaque");
            }
         }
         addr.position += region_size;
      }
   }
   printf("\n");
}