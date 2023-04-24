#pragma once
#include <ins/memory/descriptors.h>

namespace ins::mem {

   struct IMemoryConsumer {
      virtual void RescueStarvingSituation(size_t expectedByteLength) = 0;
   };

   struct RegionLayoutID {
      enum {
         // Object region
         Reserved_ObjectRegionMin = 0x00,
         Reserved_ObjectRegionMax = 0x7f,

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
         return this->value <= RegionLayoutID::Reserved_ObjectRegionMax;
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
      uint8_t segmentation = cst::ArenaSizeL2;
      uint16_t indice = 0;

      // Region allocation state
      uint32_t availables_scan_position = 0;
      std::atomic_uint32_t availables_count = 0;
      ArenaDescriptor* next = 0;

      // Region table
      RegionLayoutID regions[1] = { RegionLayoutID::FreeRegion };

      ArenaDescriptor();
      ArenaDescriptor(uint8_t sizeL2);
      void Initiate(uint8_t segmentation);
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
   *   Memory Region Heap
   *
   ***********************************************************************/
   struct MemoryRegions {
      static ArenaEntry* ArenaMap;

      static void Initiate();

      static size_t GetMaxUsablePhysicalBytes();
      static void SetMaxUsablePhysicalBytes(size_t size);

      // Global memory management
      static bool RequirePhysicalBytes(size_t size, IMemoryConsumer* consumer);
      static void ReleasePhysicalBytes(size_t size);
      static size_t GetUsedPhysicalBytes();

      // Arena management
      static address_t ReserveArena();

      // Region management
      static Descriptor* GetRegionDescriptor(address_t address);
      static size_t GetRegionSize(address_t address);
      static void ForeachRegion(std::function<bool(ArenaDescriptor* arena, RegionLayoutID layout, address_t addr)>&& visitor);
      static void PerformMemoryCleanup();

      // Standard size allocation management
      static address_t AllocateUnmanagedRegion(uint8_t sizeL2, uint8_t sizingID, IMemoryConsumer* consumer);
      static address_t AllocateManagedRegion(uint8_t sizeL2, uint8_t sizingID, IMemoryConsumer* consumer);
      static address_t ReserveUnmanagedRegion(uint8_t sizeL2);
      static address_t ReserveManagedRegion(uint8_t sizeL2);
      static void ReleaseRegion(address_t address, uint8_t sizeL2, uint8_t sizingID);
      static void DisposeRegion(address_t address, uint8_t sizeL2, uint8_t sizingID);

      // Adjusted size allocation management
      static address_t AllocateUnmanagedRegionEx(size_t size, IMemoryConsumer* consumer);
      static address_t AllocateManagedRegionEx(size_t size, IMemoryConsumer* consumer);
      static void ReleaseRegionEx(address_t address, size_t size);
      static void DisposeRegionEx(address_t address, size_t size);

      // Utils API
      static void Print();
   };

   struct RegionsSpaceInitiator {
      RegionsSpaceInitiator();
   };

   extern"C" MemoryRegions Regions;

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
         auto entry = Regions.ArenaMap[address.arenaID];
         return RegionLocation(entry, address.position >> entry.segmentation);
      }
   };

}