#include <ins/memory/regions.h>
#include <ins/memory/contexts.h>
#include <ins/memory/controller.h>
#include <ins/os/memory.h>

using namespace ins;

MemoryRegionHeap ins::RegionsHeap;

ArenaDescriptor ins::ArenaDescriptor::UnusedArena;
ArenaDescriptor ins::ArenaDescriptor::ForbiddenArena;

ins::ArenaDescriptor::ArenaDescriptor()
   : Descriptor(), indice(0), segmentation(cst::ArenaSizeL2)
{
   this->availables_count = 1;
   this->regions[0] = RegionLayoutID::FreeRegion;
}

ins::ArenaDescriptor::ArenaDescriptor(uint8_t segmentation)
   : Descriptor(DescriptorTypeID::Arena), indice(0), segmentation(segmentation)
{
   this->availables_count = cst::ArenaSize >> segmentation;
   memset(this->regions, RegionLayoutID::FreeRegion, this->availables_count);
}

const char* RegionLayoutID::GetLabel() {
   if (this->value <= RegionLayoutID::ObjectRegionMax) {
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
   auto& infos = ins::cst::RegionSizingInfos[index];
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

address_t ArenaClassPool::AllocateRegion(uint8_t sizingID, MemoryContext* context) {
   if (this->caches[sizingID].size()) {
      auto addr = this->caches[sizingID].PopRegion();
      if (addr) return addr;
   }
   auto committedSize = this->sizings[sizingID].committedSize;
   auto committedCount = committedSize < cst::PageSize ? cst::PageSize / committedSize : 1;
   if (ins::RegionsHeap.RequirePhysicalBytes(committedSize, context)) {
      auto ptr = this->AcquireRegionRange(RegionLayoutID::FreeCachedRegion);
      OSMemory::CommitMemory(ptr, committedSize);
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
      throw ins::exception_missing_memory();
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
      address_t base = ins::RegionsHeap.ReserveArena();
      if (!base) throw std::exception("OOM");
      arena = Descriptor::NewBuffer<ArenaDescriptor>(ArenaDescriptor::GetDescriptorSize(this->sizeL2), this->sizeL2);
      arena->indice = base.arenaID;
      arena->next = this->availables;
      if (this->managed) {
         auto bitmapLen = arena->availables_count * 8;
         arena->managed = true;
      }
      this->availables = arena;
      ins::RegionsHeap.arenas[base.arenaID] = arena;
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

address_t ArenaClassPool::AllocateRegionEx(size_t size, MemoryContext* context) {
   _ASSERT(size <= this->sizings[0].committedSize);
   auto pages = size >> this->pageSizeL2;
   if (size > (pages << this->pageSizeL2)) {
      pages++;
   }
   for (int i = 0; i < 4; i++) {
      if (this->sizings[i].committedPages == pages) {
         return this->AllocateRegion(i, context);
      }
   }
   auto committedSize = pages << this->pageSizeL2;
   if (ins::RegionsHeap.RequirePhysicalBytes(committedSize, context)) {
      auto address = this->ReserveRegion();
      _ASSERT(committedSize <= this->sizings[0].committedSize);
      OSMemory::CommitMemory(address, committedSize);
      return address;
   }
   else {
      throw ins::exception_missing_memory();
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
   OSMemory::DecommitMemory(address.ptr, size);
   ins::RegionsHeap.ReleasePhysicalBytes(size);
   loc.layout() = RegionLayoutID::FreeRegion;
}

/**********************************************************************
*
*   Memory Region Heap
*
***********************************************************************/

namespace ins {
   void __notify_memory_item_init__(uint32_t flag);
}

static size_t GetBufferRegionSizing(size_t size) {
   return ins::log2_ceil_32(size);
}

MemoryRegionHeap::MemoryRegionHeap() {
   if (this == &ins::RegionsHeap) {
      __notify_memory_item_init__(1);
   }
   else {
      printf("! MemoryRegionHeap is singleton !\n");
      exit(1);
   }
}

void MemoryRegionHeap::Initiate() {
   for (int i = 0; i < cst::RegionSizingCount; i++) {
      this->arenas_unmanaged[i].Initiate(i, false);
      this->arenas_managed[i].Initiate(i, true);
   }
   for (int i = 0; i < cst::ArenaPerSpace; i++) {
      this->arenas[i] = ArenaEntry(&ArenaDescriptor::UnusedArena);
   }
}

bool MemoryRegionHeap::RequirePhysicalBytes(size_t size, MemoryContext* context) {
   this->usedPhysicalBytes += size;
   if (this->usedPhysicalBytes > this->maxUsablePhysicalBytes) {
      ins::StarvedConsumerToken token;
      token.expectedByteLength = size;
      token.context = context;
      ins::Controller.RescueStarvedConsumer(token);
      if (this->usedPhysicalBytes > this->maxUsablePhysicalBytes) {
         this->usedPhysicalBytes -= size;
         return false;
      }
   }
   return true;
}

void MemoryRegionHeap::ReleasePhysicalBytes(size_t size) {
   this->usedPhysicalBytes -= size;
}

size_t MemoryRegionHeap::GetUsedPhysicalBytes() {
   return this->usedPhysicalBytes;
}

Descriptor* MemoryRegionHeap::GetRegionDescriptor(address_t address) {
   if (auto arena = this->arenas[address.arenaID]) {
      auto regionID = address.position >> arena.segmentation;
      auto regionEntry = arena.descriptor()->regions[regionID];
      if (!regionEntry.IsFree()) {
         address.position = regionID << arena.segmentation;
         return (Descriptor*)address.ptr;
      }
   }
   return 0;
}

size_t MemoryRegionHeap::GetRegionSize(address_t address) {
   auto arena = this->arenas[address.arenaID];
   return size_t(1) << arena.segmentation;
}

address_t MemoryRegionHeap::ReserveArena() {
   return OSMemory::ReserveMemory(0, cst::SpaceSize, cst::ArenaSize, cst::ArenaSize);
}

address_t MemoryRegionHeap::ReserveUnmanagedRegion(uint8_t sizeL2) {
   return this->arenas_unmanaged[sizeL2].ReserveRegion();
}

address_t MemoryRegionHeap::ReserveManagedRegion(uint8_t sizeL2) {
   return this->arenas_managed[sizeL2].ReserveRegion();
}

address_t MemoryRegionHeap::AllocateUnmanagedRegion(uint8_t sizeL2, uint8_t sizingID, MemoryContext* context) {
   return this->arenas_unmanaged[sizeL2].AllocateRegion(sizingID, context);
}

address_t MemoryRegionHeap::AllocateManagedRegion(uint8_t sizeL2, uint8_t sizingID, MemoryContext* context) {
   return this->arenas_managed[sizeL2].AllocateRegion(sizingID, context);
}

void MemoryRegionHeap::ReleaseRegion(address_t address, uint8_t sizeL2, uint8_t sizingID) {
   auto arena = this->arenas[address.arenaID];
   if (arena.segmentation != sizeL2) {
      throw "invalid sizeL2";
   }
   else {
      auto list = arena.managed ? this->arenas_managed : this->arenas_unmanaged;
      list[arena.segmentation].ReleaseRegion(address, sizingID);
   }
}

void MemoryRegionHeap::DisposeRegion(address_t address, uint8_t sizeL2, uint8_t sizingID) {
   auto arena = this->arenas[address.arenaID];
   if (arena.segmentation != sizeL2) {
      throw "invalid sizeL2";
   }
   else {
      auto list = arena.managed ? this->arenas_managed : this->arenas_unmanaged;
      list[arena.segmentation].DisposeRegion(address, sizingID);
   }
}

address_t MemoryRegionHeap::AllocateUnmanagedRegionEx(size_t size, MemoryContext* context) {
   auto sizeL2 = GetBufferRegionSizing(size);
   return this->arenas_unmanaged[sizeL2].AllocateRegionEx(size, context);
}

address_t MemoryRegionHeap::AllocateManagedRegionEx(size_t size, MemoryContext* context) {
   auto sizeL2 = GetBufferRegionSizing(size);
   return this->arenas_managed[sizeL2].AllocateRegionEx(size, context);
}

void MemoryRegionHeap::ReleaseRegionEx(address_t address, size_t size) {
   auto arena = this->arenas[address.arenaID];
   auto sizeL2 = GetBufferRegionSizing(size);
   if (arena.segmentation != sizeL2) {
      throw "invalid sizeL2";
   }
   else {
      auto list = arena.managed ? this->arenas_managed : this->arenas_unmanaged;
      list[arena.segmentation].ReleaseRegionEx(address, size);
   }
}

void MemoryRegionHeap::DisposeRegionEx(address_t address, size_t size) {
   auto arena = this->arenas[address.arenaID];
   auto sizeL2 = GetBufferRegionSizing(size);
   if (arena.segmentation != sizeL2) {
      throw "invalid sizeL2";
   }
   else {
      auto list = arena.managed ? this->arenas_managed : this->arenas_unmanaged;
      list[arena.segmentation].DisposeRegionEx(address, size);
   }
}

void MemoryRegionHeap::PerformMemoryCleanup() {
   for (int i = 0; i < cst::RegionSizingCount; i++) {
      this->arenas_unmanaged[i].Clean();
      this->arenas_managed[i].Clean();
   }
}

void MemoryRegionHeap::ForeachRegion(std::function<bool(ArenaDescriptor* arena, RegionLayoutID layout, address_t addr)>&& visitor) {
   for (address_t addr; addr.arenaID < cst::ArenaPerSpace; addr.arenaID++) {
      auto arena = this->arenas[addr.arenaID].descriptor();
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

void MemoryRegionHeap::Print() {
   ins::RegionsHeap.ForeachRegion(
      [&](ArenaDescriptor* arena, RegionLayoutID layout, address_t addr) {
         if (layout.IsObjectRegion()) {
            auto region = ObjectRegion(addr.ptr);
            region->DisplayToConsole();
         }
         else {
            printf("\n%X%.8llX %s: %s", int(addr.arenaID), int64_t(addr.position), sz2a(size_t(1) << arena->segmentation).c_str(), layout.GetLabel());
         }
         return true;
      }
   );
   printf("\n");

   auto usedBytes = ins::RegionsHeap.GetUsedPhysicalBytes();
   printf("Memory used: %s\n", sz2a(usedBytes).c_str());
}

