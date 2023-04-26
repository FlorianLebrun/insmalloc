#pragma once

namespace ins::mem {

   /**********************************************************************
   *
   *   Arena Region Cache
   *
   ***********************************************************************/
   struct ArenaRegionCache {
   private:
      typedef struct sRegionChain {
         sRegionChain* next;
      } *RegionChain;
      std::mutex lock;
      RegionChain list = 0;
      size_t count = 0;
   public:
      size_t size() {
         return this->count;
      }
      void PushRegion(address_t ptr) {
         std::lock_guard<std::mutex> guard(this->lock);
         auto region = ptr.as<sRegionChain>();
         region->next = this->list;
         this->list = region;
         this->count++;
      }
      address_t PopRegion() {
         std::lock_guard<std::mutex> guard(this->lock);
         if (auto region = this->list) {
            this->list = region->next;
            this->count--;
            return region;
         }
         return address_t();
      }
   };

   /**********************************************************************
   *
   *   Arena Class Pool
   *
   ***********************************************************************/
   struct ArenaClassPool {
      typedef tRegionSizingInfos::tSizing tSizing;

      uint8_t pageSizeL2 = 0;
      tSizing sizings[4];
      uint8_t sizeL2 = 0;
      ArenaRegionCache caches[4];
      ArenaDescriptor* availables = 0;
      uint16_t batchSizeL2 = 0;
      bool managed = false;
      std::mutex lock;

      void Initialize(uint8_t index, bool managed);
      void Clean();

      // Region management
      address_t ReserveRegion();
      address_t AllocateRegion(uint8_t sizingID, IMemoryConsumer* consumer);
      void DisposeRegion(address_t addr, uint8_t sizingID);
      void CacheRegion(address_t addr, uint8_t sizingID);
      void ReleaseRegion(address_t addr, uint8_t sizingID);

      // Buffer management
      address_t AllocateRegionEx(size_t size, IMemoryConsumer* consumer);
      void DisposeRegionEx(address_t address, size_t size);
      void ReleaseRegionEx(address_t address, size_t size);

   private:
      address_t AcquireRegionRange(uint8_t layoutID);
   };

   /**********************************************************************
   *
   *   Memory Descriptor
   *
   ***********************************************************************/
   struct MemoryDescriptor {

      std::mutex lock;
      std::atomic_size_t usedPhysicalBytes = 0;
      size_t maxUsablePhysicalBytes = size_t(1) << 34;

      ArenaEntry* arenas_map = 0; // ArenaEntry[cst::ArenaPerSpace]

      ArenaDescriptor descriptors_arena;
      DescriptorsAllocator descriptors_allocator;

      ArenaClassPool arenas_unmanaged[cst::RegionSizingCount];
      ArenaClassPool arenas_managed[cst::RegionSizingCount];

      MemoryDescriptor(uint32_t arena_pagecountL2) {

         // Initialize arena map
         auto arenas_map_size = sizeof(ArenaEntry) * cst::ArenaPerSpace;
         this->arenas_map = (ArenaEntry*)os::AllocateMemory(0, cst::SpaceSize, arenas_map_size, cst::PageSize);
         for (int i = 0; i < cst::ArenaPerSpace; i++) {
            this->arenas_map[i] = ArenaEntry(&ArenaDescriptor::UnusedArena);
         }

         // Register descriptors arena
         this->descriptors_arena.Initialize(cst::ArenaSizeL2);
         this->descriptors_arena.indice = address_t(this).arenaID;
         this->descriptors_arena.availables_count--;
         this->descriptors_arena.regions[0] = RegionLayoutID::DescriptorHeapRegion;
         this->arenas_map[address_t(this).arenaID] = ArenaEntry(&this->descriptors_arena);
         _ASSERT(this->descriptors_arena.availables_count == 0);

         // Initialize descriptors allocator
         this->descriptors_allocator.Initialize(uintptr_t(this), sizeof(MemoryDescriptor), arena_pagecountL2);

         // Initialize regions allocator
         for (int i = 0; i < cst::RegionSizingCount; i++) {
            this->arenas_unmanaged[i].Initialize(i, false);
            this->arenas_managed[i].Initialize(i, true);
         }
      }

      static MemoryDescriptor* New() {
         size_t base_sizeL2 = bit::log2_ceil_32(sizeof(MemoryDescriptor));
         if (base_sizeL2 < cst::PageSizeL2) base_sizeL2 = cst::PageSizeL2;

         address_t buffer = mem::ReserveArena();
         os::CommitMemory(buffer, size_t(1) << base_sizeL2);

         auto space = new((void*)buffer) MemoryDescriptor(0);
         return space;
      }
   };

   extern MemoryDescriptor* space;
}