#pragma once
#include <ins/memory/descriptors.h>
#include <ins/memory/config.h>

namespace ins {
   struct MemoryContext;

   struct RegionLayoutID {
      enum {
         // Object region
         ObjectRegionMin = cst::ObjectLayoutMin,
         ObjectRegionMax = cst::ObjectLayoutMax,

         // Specific region
         BufferRegion = 0x80,
         DescriptorHeapRegion = 0x81,

         // Space management region
         FreeRegion = 0xff,
         FreeCachedRegion = 0xfe,
      };
      uint8_t value;
      RegionLayoutID(uint8_t value = 0)
         : value(value) {
      }
      bool IsFree() {
         return this->value == RegionLayoutID::FreeRegion;
      }
      bool IsObjectRegion() {
         return this->value <= RegionLayoutID::ObjectRegionMax;
      }
      void operator = (uint8_t value) {
         this->value = value;
      }
      operator uint8_t() {
         return this->value;
      }
      const char* GetLabel();
   };
   static_assert(sizeof(RegionLayoutID) == 1, "bad size");

   struct tRegionSizingInfos {
      struct tSizing {
         uint32_t retention = 0;
         uint32_t committedPages = 0;
         size_t committedSize = 0;
      };
      uint8_t granularityL2 = 0;
      uint8_t pageSizeL2 = 0;
      tSizing sizings[4];
   };

   namespace cst {
      extern const tRegionSizingInfos RegionSizingInfos[RegionSizingCount];
      extern const uint32_t RegionMasks[RegionSizingCount];
   }

   /**********************************************************************
   *
   *   Arena Descriptor
   *
   ***********************************************************************/
   struct ArenaDescriptor : Descriptor {
      static ArenaDescriptor UnusedArena;
      static ArenaDescriptor ForbiddenArena;

      // Arena location
      bool managed = false;
      uint8_t segmentation = 0;
      uint16_t indice = 0;

      // Region allocation state
      uint32_t availables_scan_position = 0;
      std::atomic_uint32_t availables_count = 0;
      ArenaDescriptor* next = 0;

      // Region table
      RegionLayoutID regions[1];

      ArenaDescriptor();
      ArenaDescriptor(uint8_t sizeL2);
      size_t GetRegionCount() {
         return size_t(1) << (cst::ArenaSizeL2 - this->segmentation);
      }
      static size_t GetDescriptorSize(uint8_t sizeL2) {
         return sizeof(ArenaDescriptor) + (cst::ArenaSize >> sizeL2) * sizeof(RegionLayoutID);
      }
   };

   /**********************************************************************
   *
   *   Arena Table Entry
   *
   ***********************************************************************/
   union ArenaEntry {
      uint64_t bits;
      struct {
         uint64_t segmentation : 8;
         uint64_t managed : 1;
         uint64_t reference : 55;
      };
      ArenaEntry()
         : bits(0) {
      }
      ArenaEntry(ArenaDescriptor* desc)
         : reference(uint64_t(desc)), segmentation(desc->segmentation), managed(desc->managed) {
      }
      ArenaDescriptor* descriptor() {
         return (ArenaDescriptor*)this->reference;
      }
      RegionLayoutID& layout(size_t regionIndex) {
         return ((ArenaDescriptor*)this->reference)->regions[regionIndex];
      }
      operator bool() {
         return this->bits != 0;
      }
   };
   static_assert(sizeof(ArenaEntry) == 8, "bad size");

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
      
      void Initiate(uint8_t index, bool managed);
      void Clean();

      // Region management
      address_t ReserveRegion();
      address_t AllocateRegion(uint8_t sizingID, MemoryContext* context);
      void DisposeRegion(address_t addr, uint8_t sizingID);
      void CacheRegion(address_t addr, uint8_t sizingID);
      void ReleaseRegion(address_t addr, uint8_t sizingID);

      // Buffer management
      address_t AllocateRegionEx(size_t size, MemoryContext* context);
      void DisposeRegionEx(address_t address, size_t size);
      void ReleaseRegionEx(address_t address, size_t size);

   private:
      address_t AcquireRegionRange(uint8_t layoutID);
   };


   /**********************************************************************
   *
   *   Memory Region Heap
   *
   ***********************************************************************/
   struct MemoryRegionHeap {
      std::mutex lock;
      std::atomic_size_t usedPhysicalBytes = 0;
      size_t maxUsablePhysicalBytes = size_t(1) << 34;

      ArenaClassPool arenas_unmanaged[cst::RegionSizingCount];
      ArenaClassPool arenas_managed[cst::RegionSizingCount];

      ArenaEntry arenas[cst::ArenaPerSpace];

      MemoryRegionHeap();
      void Initiate();

      // Global memory management
      bool RequirePhysicalBytes(size_t size, MemoryContext* context);
      void ReleasePhysicalBytes(size_t size);
      size_t GetUsedPhysicalBytes();

      // Arena management
      address_t ReserveArena();

      // Region management
      Descriptor* GetRegionDescriptor(address_t address);
      size_t GetRegionSize(address_t address);
      void ForeachRegion(std::function<bool(ArenaDescriptor* arena, RegionLayoutID layout, address_t addr)>&& visitor);
      void PerformMemoryCleanup();

      // Standard size allocation management
      address_t AllocateUnmanagedRegion(uint8_t sizeL2, uint8_t sizingID, MemoryContext* context);
      address_t AllocateManagedRegion(uint8_t sizeL2, uint8_t sizingID, MemoryContext* context);
      address_t ReserveUnmanagedRegion(uint8_t sizeL2);
      address_t ReserveManagedRegion(uint8_t sizeL2);
      void ReleaseRegion(address_t address, uint8_t sizeL2, uint8_t sizingID);
      void DisposeRegion(address_t address, uint8_t sizeL2, uint8_t sizingID);

      // Adjusted size allocation management
      address_t AllocateUnmanagedRegionEx(size_t size, MemoryContext* context);
      address_t AllocateManagedRegionEx(size_t size, MemoryContext* context);
      void ReleaseRegionEx(address_t address, size_t size);
      void DisposeRegionEx(address_t address, size_t size);

      // Utils API
      void Print();
   };

   extern MemoryRegionHeap RegionsHeap;

   /**********************************************************************
   *
   *   Region Location
   *
   ***********************************************************************/
   struct RegionLocation {
      ArenaEntry entry;
      uintptr_t index;
      RegionLocation(ArenaEntry entry, uintptr_t index)
         : entry(entry), index(index) {
      }
      ArenaDescriptor* arena() {
         return this->entry.descriptor();
      }
      RegionLayoutID& layout() {
         return this->entry.layout(this->index);
      }
      uintptr_t position() {
         return this->index << this->entry.segmentation;
      }
      static RegionLocation New(address_t address) {
         auto entry = ins::RegionsHeap.arenas[address.arenaID];
         return RegionLocation(entry, address.position >> entry.segmentation);
      }
   };

}