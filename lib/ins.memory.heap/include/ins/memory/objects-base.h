#pragma once
#include <ins/memory/descriptors.h>
#include <ins/memory/structs.h>
#include <ins/memory/map.h>
#include <ins/memory/config.h>
#include <stdint.h>
#include <stdlib.h>

namespace ins::mem {

   typedef struct sObjectHeader* ObjectHeader;
   typedef struct sObjectRegion* ObjectRegion;
   typedef struct sObjectSchema* ObjectSchema;
   typedef uint8_t* ObjectBytes;
   struct ObjectLocalContext;
   struct MemoryContext;

   enum ObjectLayoutPolicy {
      SmallObjectPolicy, // region with multiple objects
      MediumObjectPolicy, // region with one object
      LargeObjectPolicy, // region with one object and no caching policy
   };

   namespace cst {
      const size_t ObjectPerSlabL2 = 6;
      const size_t ObjectPerSlab = size_t(1) << ObjectPerSlabL2;
      const size_t ObjectRegionHeadSize = 64;
   }

   // Object size fixed point divider: index = (uint64_t(position)*ObjectDividerFixed32[clsID]) >> 32 
   struct tObjectLayoutBase {
      static constexpr uint8_t DividerShift = 35;
      uint32_t object_divider;
      uint32_t object_multiplier;
      uintptr_t GetObjectIndex(uintptr_t offset) const {
         return (uint64_t(offset - cst::ObjectRegionHeadSize) * object_divider) >> DividerShift;
      }
      uintptr_t GetObjectOffset(uintptr_t index) const {
         return cst::ObjectRegionHeadSize + index * object_multiplier;
      }
   };

   struct tObjectLayoutInfos {
      uint8_t region_objects;
      uint8_t region_templateID;
      uint8_t region_sizeL2;
      uint8_t region_sizingID;
      ObjectLayoutPolicy policy;
      struct {
         uint32_t list_length = 0;
         uint32_t heap_count = 0;
         uint32_t context_count = 0;
      } retention;
   };

   struct tObjectRegionTemplate {
      uint8_t region_sizeL2;
      uint8_t region_sizingID;
   };

   namespace cst {
      extern const size_t ObjectLayoutCount;
      extern const tObjectLayoutInfos ObjectLayoutInfos[];
      extern const tObjectLayoutBase ObjectLayoutBase[];
      extern const uint64_t ObjectLayoutMask[];
      extern const tObjectRegionTemplate ObjectRegionTemplate[];
   }

   /**********************************************************************
   *
   *   Object Header
   *
   ***********************************************************************/
   struct sObjectHeader {
      static const uint64_t cZeroHeaderBits = 0;
      static const uint64_t cDeadHeaderBits = -1;
      enum FLAGS : uint64_t {
         SCHEMA_ID_MASK = 0xffffff,
         CLUSTER_ID_MASK = 0xff000000,
         HAS_ANALYTICS_INFOS = 0x0100000000,
         HAS_SECURITY_PADDING = 0x0200000000,
         HAS_LOCK = 0x0400000000,
      };
      union {
         struct {
            uint32_t schema_id; // see ObjectSchemaID
            uint8_t cluster_id; // define the object cluster id, when 0 means object is not clustered
            uint8_t has_analytics_infos : 1; // define that object is followed by analytics structure
            uint8_t has_security_padding : 1; // define that object is followed by a padding of security(canary)
            uint8_t lock : 1; // general multithread protection for this object
            uint8_t hard_retention;
            uint8_t weak_retention;
         };
         struct {
            uint32_t __resv__0;
            uint16_t __resv__1;
            uint16_t retention;
         };
         uint64_t bits;
      };
      void* ptr() {
         return &this[1];
      }
      bool IsDisposable() {
         return this->retention == 0;
      }
   };
   static_assert(sizeof(sObjectHeader) == sizeof(uint64_t), "bad size");


   /**********************************************************************
   *
   *   Object Region
   *
   ***********************************************************************/

   struct sObjectRegion {
      uint8_t layoutID = 0; // Region layout class
      uint8_t notified_finalizers = 0; // Notified: object with pending finalize shall be check in gc state
      uint32_t width = 0; // Region size based on arena granularity metric
      ObjectLocalContext* owner = 0; // Region owner

      // Availability bitmap
      uint64_t availables = 0; // Availability bits of free objects
      std::atomic_uint64_t notified_availables = 0; // Notified: Availability bits from other thread

      // Region list chaining
      struct {
         sObjectRegion* used = none<sObjectRegion>();
         sObjectRegion* notified = none<sObjectRegion>();
      } next;

      void NotifyAvailables(bool managed);

      void DisplayToConsole();
      void Dispose();

      bool IsDisposable() {
         return this->GetAvailablesMap() == cst::ObjectLayoutMask[this->layoutID];
      }

      bool IsNotified() {
         return this->next.notified != none<sObjectRegion>();
      }

      size_t GetCount() {
         return cst::ObjectLayoutInfos[this->layoutID].region_objects;
      }

      uint64_t GetAvailablesMap() {
         return this->availables | this->notified_availables.load(std::memory_order_relaxed);
      }

      size_t GetAvailablesCount() {
         return bit::bitcount_64(this->GetAvailablesMap());
      }

      size_t GetUsedCount() {
         auto bits = this->GetAvailablesMap() ^ cst::ObjectLayoutMask[this->layoutID];
         return bit::bitcount_64(bits);
      }

      size_t GetNotifiedCount() {
         auto bits = this->notified_availables.load(std::memory_order_relaxed);
         return bit::bitcount_64(bits);
      }

      bool IsObjectAvailable(uint64_t object_bit) {
         return (this->availables & object_bit ||
            this->notified_availables.load(std::memory_order_relaxed) & object_bit);
      }

      size_t GetObjectSize() {
         return cst::ObjectLayoutBase[this->layoutID].object_multiplier;
      }

      size_t GetRegionSize() {
         auto regionID = mem::ArenaMap[address_t(this).arenaID].segmentation;
         return size_t(this->width) << mem::cst::RegionSizingInfos[regionID].granularityL2;
      }

      ObjectHeader GetObjectAt(int index) {
         auto offset = cst::ObjectLayoutBase[this->layoutID].GetObjectOffset(index);
         return ObjectHeader(ObjectBytes(this) + offset);
      }

      __forceinline ObjectHeader AcquireObject() {
         if (this->availables) {

            // Get an object index
            auto index = bit::lsb_64(this->availables);

            // Prepare new object
            auto offset = cst::ObjectRegionHeadSize + index * cst::ObjectLayoutBase[this->layoutID].object_multiplier;
            auto obj = ObjectHeader(&ObjectBytes(this)[offset]);

            // Publish object as ready
            auto bit = uint64_t(1) << index;
            this->availables ^= bit;

            return obj;
         }
         else {
            return 0;
         }
      }

      static sObjectRegion* New(bool managed, uint8_t layoutID, ObjectLocalContext* owner);
      static sObjectRegion* New(bool managed, uint8_t layoutID, size_t size, ObjectLocalContext* owner);

   protected:
      friend struct Descriptor;

      sObjectRegion(uint8_t layoutID, size_t size, ObjectLocalContext* owner)
         : layoutID(layoutID), owner(owner) {

         auto& infos = cst::ObjectLayoutInfos[layoutID];
         this->width = size >> mem::cst::RegionSizingInfos[infos.region_sizeL2].granularityL2;
         this->availables = mem::cst::ObjectLayoutMask[layoutID];
         _INS_TRACE(printf("! new region: %p: object_size=%d object_count=%d\n", this, int(layoutID), int(infos.region_objects)));
      }

   };

   static_assert(sizeof(sObjectRegion) <= cst::ObjectRegionHeadSize, "bad size");

   /**********************************************************************
   *
   *   Object Location & Infos
   *
   ***********************************************************************/

   struct ObjectAnalyticsInfos {
      uint64_t stackstamp;
      uint64_t timestamp;
      ObjectAnalyticsInfos(uint64_t timestamp = 0, uint64_t stackstamp = 0) {
         this->stackstamp = stackstamp;
         this->timestamp = timestamp;
      }
   };
   static_assert(sizeof(ObjectAnalyticsInfos) == sizeof(uint64_t) * 2, "bad size");

   struct ObjectLocation {
   public:
      ArenaEntry arena;
      ObjectHeader object;
      ObjectRegion region;
      RegionLayoutID layout;
      uint32_t index;

      bool IsAlive();
      bool IsAllocated();

      void Retain();
      void RetainWeak();

      bool ReleaseWeak(MemoryContext* context);
      bool Release(MemoryContext* context);
      bool Free(MemoryContext* context);

      ObjectLocation(address_t address) {
         this->arena = mem::ArenaMap[address.arenaID];
         this->layout = this->arena.layout(address.position >> this->arena.segmentation);
         if (this->layout.IsObjectRegion()) {
            auto& infos = mem::cst::ObjectLayoutBase[this->layout];
            auto offset = address.position & mem::cst::RegionMasks[this->arena.segmentation];
            this->region = ObjectRegion(address.ptr - offset);
            this->index = infos.GetObjectIndex(offset);
            if (this->index < cst::ObjectLayoutInfos[this->layout].region_objects) {
               this->object = ObjectHeader(uintptr_t(region) + infos.GetObjectOffset(this->index));
            }
            else {
               this->object = 0;
            }
         }
         else {
            this->region = 0;
            this->object = 0;
            this->index = 0;
         }
      }
   };

   struct ObjectInfos : ObjectLocation {
      ObjectInfos(address_t address)
         : ObjectLocation(address) {
      }
      size_t GetAllocatedSize() {
         if (this->object) {
            return this->region->GetObjectSize();
         }
         return 0;
      }
      size_t GetUsableSize() {
         if (this->object) {
            auto size = this->region->GetObjectSize();
            if (this->object->has_analytics_infos) {
               size -= sizeof(ObjectAnalyticsInfos);
            }
            if (this->object->has_security_padding) {
               auto paddingEnd = size - sizeof(uint32_t);
               auto pBufferSize = (uint32_t*)&ObjectBytes(object)[paddingEnd];
               uint32_t bufferSize = (*pBufferSize) ^ 0xabababab;
               if (bufferSize < size) size = bufferSize;
            }
            return size;
         }
         return 0;
      }
      ObjectAnalyticsInfos* GetAnalyticsInfos() {
         if (this->object && this->object->has_analytics_infos) {
            auto size = this->region->GetObjectSize();
            auto pinfos = &ObjectBytes(object)[size - sizeof(ObjectAnalyticsInfos)];
            return (ObjectAnalyticsInfos*)pinfos;
         }
         return 0;
      }
      void* DetectOverflowedBytes() {
         if (this->object && this->object->has_security_padding) {
            auto size = this->region->GetObjectSize();
            if (this->object->has_analytics_infos) {
               size -= sizeof(ObjectAnalyticsInfos);
            }

            // Read and check padding size
            auto paddingEnd = size - sizeof(uint32_t);
            auto pBufferSize = (uint32_t*)&ObjectBytes(object)[paddingEnd];
            uint32_t bufferSize = (*pBufferSize) ^ 0xabababab;
            if (bufferSize > size) {
               return pBufferSize;
            }

            // Read and check padding bytes
            auto bytes = ObjectBytes(object);
            for (uint32_t i = bufferSize; i < paddingEnd; i++) {
               if (bytes[i] != 0xab) {
                  return &bytes[i];
               }
            }
         }
         return 0;
      }
   };

   /**********************************************************************
   *
   *   Functions
   *
   ***********************************************************************/

   inline uint8_t getLayoutForSize(size_t size) {
      if (size < cst::SmallSizeLimit) {
         const size_t size_step = cst::SmallSizeLimit / cst::LayoutRangeSizeCount;
         auto index = (size + 7) / size_step;
         return cst::small_object_layouts[index];
      }
      else if (size < cst::MediumSizeLimit) {
         const size_t size_step = cst::MediumSizeLimit / cst::LayoutRangeSizeCount;
         auto& bin = cst::medium_object_layouts[size / size_step];
         if (size <= cst::ObjectLayoutBase[bin.layoutMin].object_multiplier) return bin.layoutMin;
         else return bin.layoutMax;
         _ASSERT(0);
      }
      else if (size < cst::LargeSizeLimit) {
         const size_t size_step = cst::LargeSizeLimit / cst::LayoutRangeSizeCount;
         auto& bin = cst::large_object_layouts[size / size_step];
         if (size <= cst::ObjectLayoutBase[bin.layoutMin].object_multiplier) return bin.layoutMin;
         else return bin.layoutMax;
         _ASSERT(0);
      }
      return cst::ObjectLayoutCount - 1;
   };


}
