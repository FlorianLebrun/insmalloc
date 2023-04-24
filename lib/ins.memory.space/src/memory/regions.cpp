#include <ins/memory/regions.h>
#include <ins/os/memory.h>
#include "./descriptors-allocator.h"
#include "./regions-allocator.h"

using namespace ins;
using namespace ins::mem;

MemoryRegions mem::Regions;
ArenaEntry* mem::MemoryRegions::ArenaMap = 0;
mem::MemoryDescriptor* mem::space = 0;

ArenaDescriptor mem::ArenaDescriptor::UnusedArena;
ArenaDescriptor mem::ArenaDescriptor::ForbiddenArena;

mem::RegionsSpaceInitiator::RegionsSpaceInitiator() {
   mem::Regions.Initiate();
}

mem::ArenaDescriptor::ArenaDescriptor()
   : Descriptor()
{
}

mem::ArenaDescriptor::ArenaDescriptor(uint8_t segmentation)
   : Descriptor(DescriptorTypeID::Arena)
{
   mem::Regions.Initiate();
   this->Initiate(segmentation);
}

void mem::ArenaDescriptor::Initiate(uint8_t segmentation) {
   this->segmentation = segmentation;
   this->availables_count = cst::ArenaSize >> segmentation;
   memset(this->regions, RegionLayoutID::FreeRegion, this->availables_count);
}

const char* RegionLayoutID::GetLabel() {
   if (this->value <= RegionLayoutID::Reserved_ObjectRegionMax) {
      return "ObjectRegion";
   }
   switch (this->value) {
   case RegionLayoutID::BufferRegion: return "BufferRegion";
   case RegionLayoutID::DescriptorHeapRegion: return "DescriptorHeapRegion";
   case RegionLayoutID::FreeRegion: return "FreeRegion";
   case RegionLayoutID::FreeCachedRegion: return "FreeCachedRegion";
   default: return "(UnkownRegion)";
   }
}

/**********************************************************************
*
*   Region Class Heap
*
***********************************************************************/

void ArenaClassPool::Initiate(uint8_t index, bool managed) {
   auto& infos = mem::cst::RegionSizingInfos[index];
   this->sizings[0] = infos.sizings[0];
   this->sizings[1] = infos.sizings[1];
   this->sizings[2] = infos.sizings[2];
   this->sizings[3] = infos.sizings[3];
   this->pageSizeL2 = infos.pageSizeL2;
   this->sizeL2 = index;
   this->managed = managed;
   if (this->pageSizeL2 > this->sizeL2) {
      this->batchSizeL2 = this->pageSizeL2 - this->sizeL2;
   }
}

void ArenaClassPool::Clean() {
   if (this->batchSizeL2) {
      printf("Cannot release batched regions\n");
   }
   else {
      for (int i = 0; i < 4; i++) {
         while (auto region = this->caches[i].PopRegion()) {
            this->ReleaseRegion(region, i);
         }
      }
   }
}

address_t ArenaClassPool::AllocateRegion(uint8_t sizingID, IMemoryConsumer* consumer) {
   if (this->caches[sizingID].size()) {
      auto addr = this->caches[sizingID].PopRegion();
      if (addr) return addr;
   }
   auto committedSize = this->sizings[sizingID].committedSize;
   auto committedCount = committedSize < cst::PageSize ? cst::PageSize / committedSize : 1;
   if (mem::Regions.RequirePhysicalBytes(committedSize, consumer)) {
      auto ptr = this->AcquireRegionRange(RegionLayoutID::FreeCachedRegion);
      os::CommitMemory(ptr, committedSize);
      if (this->batchSizeL2) {
         auto batchSize = size_t(1) << this->batchSizeL2;
         for (size_t i = 1; i < batchSize; i++) {
            auto offset = i << this->sizeL2;
            this->caches[0].PushRegion(ptr + offset);
         }
      }
      return ptr;
   }
   else {
      if (this->caches[sizingID].size()) {
         auto addr = this->caches[sizingID].PopRegion();
         if (addr) return addr;
      }
      throw mem::exception_missing_memory();
   }
}

void ArenaClassPool::DisposeRegion(address_t address, uint8_t sizingID) {
   if (this->batchSizeL2) {
      if (sizingID != 0) throw "not supported";
      this->CacheRegion(address, sizingID);
   }
   else if (this->caches[sizingID].size() > 1024) {
      auto committeSize = this->sizings[sizingID].committedSize;
      this->ReleaseRegion(address, committeSize);
   }
   else {
      this->CacheRegion(address, sizingID);
   }
}

void ArenaClassPool::CacheRegion(address_t address, uint8_t sizingID) {
   auto loc = RegionLocation::New(address);
   _ASSERT(loc.entry.segmentation == this->sizeL2);
   if (loc.position() != address.position) {
      throw std::exception("Region mis aligned");
   }
   if (loc.layout().IsFree()) {
      throw std::exception("Region not free");
   }
   loc.layout() = RegionLayoutID::FreeCachedRegion;
   this->caches[sizingID].PushRegion(address);
}

void ArenaClassPool::ReleaseRegion(address_t address, uint8_t sizingID) {
   if (this->batchSizeL2) {
      if (sizingID != 0) throw "not supported";
      throw "not supported";
   }
   else {
      this->ReleaseRegionEx(address, this->sizings[sizingID].committedSize);
   }
}

address_t ArenaClassPool::AcquireRegionRange(uint8_t layoutID) {
   std::lock_guard<std::mutex> guard(this->lock);

   // Acquire a arena with availables regions
   auto arena = this->availables;
   if (!arena) {
      address_t base = mem::Regions.ReserveArena();
      if (!base) throw std::exception("OOM");
      arena = Descriptor::NewBuffer<ArenaDescriptor>(ArenaDescriptor::GetDescriptorSize(this->sizeL2), this->sizeL2);
      arena->indice = base.arenaID;
      arena->next = this->availables;
      if (this->managed) {
         auto bitmapLen = arena->availables_count * 8;
         arena->managed = true;
      }
      this->availables = arena;
      space->arenas_map[base.arenaID] = arena;
   }

   // Remove arena from availables list
   if (0 == --arena->availables_count) {
      this->availables = arena->next;
      arena->next = 0;
   }

   // Find free region in arena 
   auto size = size_t(1) << this->sizeL2;
   auto regionPerArena = cst::ArenaSize >> this->sizeL2;
   auto scan_position = arena->availables_scan_position;
   for (int c = 0; c < regionPerArena; c++) {
      auto index = scan_position;
      if ((++scan_position) >= regionPerArena) scan_position = 0;
      if (arena->regions[index].IsFree()) {
         address_t ptr(arena->indice, index * size);
         if (this->batchSizeL2) {
            auto batchSize = size_t(1) << this->batchSizeL2;
            _ASSERT(index % batchSize == 0);
            for (int i = 0; i < batchSize; i++) {
               arena->regions[index + i] = layoutID;
            }
         }
         else {
            arena->regions[index] = layoutID;
         }
         arena->availables_scan_position = scan_position;
         return ptr;
      }
   }
   throw std::exception("crash");
}

address_t ArenaClassPool::ReserveRegion() {
   if (this->batchSizeL2) throw "cannot reserve batched region";
   return this->AcquireRegionRange(RegionLayoutID::BufferRegion);
}

address_t ArenaClassPool::AllocateRegionEx(size_t size, IMemoryConsumer* consumer) {
   _ASSERT(size <= this->sizings[0].committedSize);
   auto pages = size >> this->pageSizeL2;
   if (size > (pages << this->pageSizeL2)) {
      pages++;
   }
   for (int i = 0; i < 4; i++) {
      if (this->sizings[i].committedPages == pages) {
         return this->AllocateRegion(i, consumer);
      }
   }
   auto committedSize = pages << this->pageSizeL2;
   if (mem::Regions.RequirePhysicalBytes(committedSize, consumer)) {
      auto address = this->ReserveRegion();
      _ASSERT(committedSize <= this->sizings[0].committedSize);
      os::CommitMemory(address, committedSize);
      return address;
   }
   else {
      throw mem::exception_missing_memory();
   }
}

void ArenaClassPool::DisposeRegionEx(address_t address, size_t size) {
   _ASSERT(size <= this->sizings[0].committedSize);
   auto pages = size >> this->pageSizeL2;
   if (size > (pages << this->pageSizeL2)) {
      pages++;
   }
   for (int i = 0; i < 4; i++) {
      if (this->sizings[i].committedPages == pages) {
         return this->DisposeRegion(address, i);
      }
   }
   this->ReleaseRegion(address, pages << this->pageSizeL2);
}

void ArenaClassPool::ReleaseRegionEx(address_t address, size_t size) {
   auto loc = RegionLocation::New(address);
   _ASSERT(loc.entry.segmentation == this->sizeL2);
   if (loc.position() != address.position) {
      throw std::exception("Region mis aligned");
   }
   if (loc.layout().IsFree()) {
      throw std::exception("Region not free");
   }
   loc.layout() = RegionLayoutID::FreeCachedRegion;
   os::DecommitMemory(address.ptr, size);
   mem::Regions.ReleasePhysicalBytes(size);
   loc.layout() = RegionLayoutID::FreeRegion;
}

/**********************************************************************
*
*   Memory Region Heap
*
***********************************************************************/

static size_t GetBufferRegionSizing(size_t size) {
   return bit::log2_ceil_32(size);
}

void MemoryRegions::Initiate() {
   if (!space) {
      space = MemoryDescriptor::New();
      MemoryRegions::ArenaMap = space->arenas_map;
   }
}

void MemoryRegions::SetMaxUsablePhysicalBytes(size_t size) {
   space->maxUsablePhysicalBytes = size;
}

size_t MemoryRegions::GetMaxUsablePhysicalBytes() {
   return space->maxUsablePhysicalBytes;
}

bool MemoryRegions::RequirePhysicalBytes(size_t size, IMemoryConsumer* consumer) {
   space->usedPhysicalBytes += size;
   if (space->usedPhysicalBytes > space->maxUsablePhysicalBytes) {
      consumer->RescueStarvingSituation(size);
      if (space->usedPhysicalBytes > space->maxUsablePhysicalBytes) {
         space->usedPhysicalBytes -= size;
         return false;
      }
   }
   return true;
}

void MemoryRegions::ReleasePhysicalBytes(size_t size) {
   space->usedPhysicalBytes -= size;
}

size_t MemoryRegions::GetUsedPhysicalBytes() {
   return space->usedPhysicalBytes;
}

Descriptor* MemoryRegions::GetRegionDescriptor(address_t address) {
   if (auto arena = space->arenas_map[address.arenaID]) {
      auto regionID = address.position >> arena.segmentation;
      auto regionEntry = arena.descriptor()->regions[regionID];
      if (!regionEntry.IsFree()) {
         address.position = regionID << arena.segmentation;
         return (Descriptor*)address.ptr;
      }
   }
   return 0;
}

size_t MemoryRegions::GetRegionSize(address_t address) {
   auto arena = space->arenas_map[address.arenaID];
   return size_t(1) << arena.segmentation;
}

address_t MemoryRegions::ReserveArena() {
   return os::ReserveMemory(0, cst::SpaceSize, cst::ArenaSize, cst::ArenaSize);
}

address_t MemoryRegions::ReserveUnmanagedRegion(uint8_t sizeL2) {
   return space->arenas_unmanaged[sizeL2].ReserveRegion();
}

address_t MemoryRegions::ReserveManagedRegion(uint8_t sizeL2) {
   return space->arenas_managed[sizeL2].ReserveRegion();
}

address_t MemoryRegions::AllocateUnmanagedRegion(uint8_t sizeL2, uint8_t sizingID, IMemoryConsumer* consumer) {
   return space->arenas_unmanaged[sizeL2].AllocateRegion(sizingID, consumer);
}

address_t MemoryRegions::AllocateManagedRegion(uint8_t sizeL2, uint8_t sizingID, IMemoryConsumer* consumer) {
   return space->arenas_managed[sizeL2].AllocateRegion(sizingID, consumer);
}

void MemoryRegions::ReleaseRegion(address_t address, uint8_t sizeL2, uint8_t sizingID) {
   auto arena = space->arenas_map[address.arenaID];
   if (arena.segmentation != sizeL2) {
      throw "invalid sizeL2";
   }
   else {
      auto list = arena.managed ? space->arenas_managed : space->arenas_unmanaged;
      list[arena.segmentation].ReleaseRegion(address, sizingID);
   }
}

void MemoryRegions::DisposeRegion(address_t address, uint8_t sizeL2, uint8_t sizingID) {
   auto arena = space->arenas_map[address.arenaID];
   if (arena.segmentation != sizeL2) {
      throw "invalid sizeL2";
   }
   else {
      auto list = arena.managed ? space->arenas_managed : space->arenas_unmanaged;
      list[arena.segmentation].DisposeRegion(address, sizingID);
   }
}

address_t MemoryRegions::AllocateUnmanagedRegionEx(size_t size, IMemoryConsumer* consumer) {
   auto sizeL2 = GetBufferRegionSizing(size);
   return space->arenas_unmanaged[sizeL2].AllocateRegionEx(size, consumer);
}

address_t MemoryRegions::AllocateManagedRegionEx(size_t size, IMemoryConsumer* consumer) {
   auto sizeL2 = GetBufferRegionSizing(size);
   return space->arenas_managed[sizeL2].AllocateRegionEx(size, consumer);
}

void MemoryRegions::ReleaseRegionEx(address_t address, size_t size) {
   auto arena = space->arenas_map[address.arenaID];
   auto sizeL2 = GetBufferRegionSizing(size);
   if (arena.segmentation != sizeL2) {
      throw "invalid sizeL2";
   }
   else {
      auto list = arena.managed ? space->arenas_managed : space->arenas_unmanaged;
      list[arena.segmentation].ReleaseRegionEx(address, size);
   }
}

void MemoryRegions::DisposeRegionEx(address_t address, size_t size) {
   auto arena = space->arenas_map[address.arenaID];
   auto sizeL2 = GetBufferRegionSizing(size);
   if (arena.segmentation != sizeL2) {
      throw "invalid sizeL2";
   }
   else {
      auto list = arena.managed ? space->arenas_managed : space->arenas_unmanaged;
      list[arena.segmentation].DisposeRegionEx(address, size);
   }
}

void MemoryRegions::PerformMemoryCleanup() {
   for (int i = 0; i < cst::RegionSizingCount; i++) {
      space->arenas_unmanaged[i].Clean();
      space->arenas_managed[i].Clean();
   }
}

void MemoryRegions::ForeachRegion(std::function<bool(ArenaDescriptor* arena, RegionLayoutID layout, address_t addr)>&& visitor) {
   for (address_t addr; addr.arenaID < cst::ArenaPerSpace; addr.arenaID++) {
      auto arena = space->arenas_map[addr.arenaID].descriptor();
      auto region_size = size_t(1) << arena->segmentation;
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

void MemoryRegions::Print() {
   mem::Regions.ForeachRegion(
      [&](ArenaDescriptor* arena, RegionLayoutID layout, address_t addr) {
         printf("\n%X%.8llX %s: %s", int(addr.arenaID), int64_t(addr.position), sz2a(size_t(1) << arena->segmentation).c_str(), layout.GetLabel());
         return true;
      }
   );
   printf("\n");

   auto usedBytes = mem::Regions.GetUsedPhysicalBytes();
   printf("Memory used: %s\n", sz2a(usedBytes).c_str());
}

