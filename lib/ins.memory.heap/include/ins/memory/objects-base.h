#pragma once
#include <ins/memory/descriptors.h>
#include <ins/memory/structs.h>
#include <ins/memory/regions.h>
#include <ins/memory/config.h>
#include <stdint.h>
#include <stdlib.h>

namespace ins::mem {

   typedef struct sObjectHeader* ObjectHeader;
   typedef struct sObjectRegion* ObjectRegion;
   typedef uint8_t* ObjectBytes;
   struct ObjectLocalContext;

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
      uint32_t object_divider;
      uint32_t object_multiplier;
      uintptr_t GetObjectIndex(uintptr_t offset) const {
         return ((offset - cst::ObjectRegionHeadSize) * object_divider) >> 16;
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
      union {
         struct {
            // Not thread protected bits (changed only on alloc/free, ie. when only one thread point to it)
            uint32_t schemaID : 24;
            uint32_t hasAnalyticsInfos : 1; // define that object is followed by analytics structure
            uint32_t hasSecurityPadding : 1; // define that object is followed by a padding of security(canary)

            // Thread protected bits
            uint32_t retentionCounter : 8; // define how many times the object shall be dispose to be really deleted
            uint32_t weakRetentionCounter : 6; // define how many times the object shall be unlocked before the memory is reusable
            uint32_t lock : 1; // general multithread protection for this object
         };
         uint64_t bits;
      };
      void* ptr() {
         return &this[1];
      }
   };
   static_assert(sizeof(sObjectHeader) == sizeof(uint64_t), "bad size");

   typedef struct sObjectMarks {
      union {
         struct {
            uint32_t marks; // Bit[k]: block k is gc marked entries
            uint32_t analyzis; // Bit[k]: block k is gc analyzed entries
         };
         uint64_t bits[1];
      };
      void Init() {
         this->bits[0] = 0;
      }
   } *ObjectMarks;
   static_assert(sizeof(sObjectMarks) == sizeof(uint64_t), "bad size");

   /**********************************************************************
   *
   *   Object Region
   *
   ***********************************************************************/

   struct sObjectRegion {
      uint8_t layoutID = 0; // Region layout class
      uint8_t privated = 0; // Region owning, 1: private, 0: shared
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
         auto bits = this->availables | this->notified_availables.load(std::memory_order_relaxed);
         return bits == cst::ObjectLayoutMask[this->layoutID];
      }

      bool IsNotified() {
         return this->next.notified != none<sObjectRegion>();
      }

      size_t GetCount() {
         return cst::ObjectLayoutInfos[this->layoutID].region_objects;
      }

      size_t GetAvailablesCount() {
         auto bits = this->availables | this->notified_availables.load(std::memory_order_relaxed);
         return bit::bitcount_64(bits);
      }

      size_t GetUsedCount() {
         auto bits = this->availables | this->notified_availables.load(std::memory_order_relaxed);
         bits ^= cst::ObjectLayoutMask[this->layoutID];
         return bit::bitcount_64(bits);
      }

      size_t GetNotifiedCount() {
         auto bits = this->notified_availables.load(std::memory_order_relaxed);
         return bit::bitcount_64(bits);
      }

      size_t GetObjectSize() {
         return cst::ObjectLayoutBase[this->layoutID].object_multiplier;
      }

      size_t GetRegionSize() {
         auto regionID = mem::Regions.ArenaMap[address_t(this).arenaID].segmentation;
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
         _INS_DEBUG(printf("! new region: %p: object_size=%zu object_count=%d\n", this, infos.object_size, infos.object_count));

      }

   };

   static_assert(sizeof(sObjectRegion) <= cst::ObjectRegionHeadSize, "bad size");

   // Object Region List
   struct ObjectRegionList {
      ObjectRegion first = 0;
      ObjectRegion last = 0;
      uint32_t count = 0;
      uint32_t limit = 0;

      void Push(ObjectRegion region) {
         region->next.used = 0;
         if (!this->last) this->first = region;
         else this->last->next.used = region;
         this->last = region;
         this->count++;
      }
      ObjectRegion Pop() {
         if (auto region = this->first) {
            this->first = region->next.used;
            if (!this->first) this->last = 0;
            this->count--;
            region->next.used = none<sObjectRegion>();
            return region;
         }
         return 0;
      }
      void DisposeAll() {
         while (auto region = this->Pop()) {
            region->Dispose();
         }
      }
      void CollectDisposables(ObjectRegionList& disposables) {
         ObjectRegion* pregion = &this->first;
         ObjectRegion last = 0;
         while (*pregion) {
            auto region = *pregion;
            if (region->IsDisposable()) {
               *pregion = region->next.used;
               this->count--;
               disposables.Push(region);
            }
            else {
               last = region;
               pregion = &region->next.used;
            }
         }
         this->last = last;
      }
      void DumpInto(ObjectRegionList& receiver, ObjectLocalContext* owner) {
         if (this->first) {
            for (ObjectRegion region = this->first; region; region = region->next.used) {
               region->owner = owner;
            }
            if (receiver.last) {
               receiver.last->next.used = this->first;
               receiver.last = this->last;
               receiver.count += this->count;
            }
            else {
               receiver.first = this->first;
               receiver.last = this->last;
               receiver.count = this->count;
            }
            this->first = 0;
            this->last = 0;
            this->count = 0;
         }
      }
      void CheckValidity() {
         uint32_t c_count = 0;
         for (auto x = this->first; x && c_count < 1000000; x = x->next.used) {
            if (!x->next.used) _ASSERT(x == this->last);
            c_count++;
         }
         _ASSERT(c_count == this->count);
      }
   };

   // Object Notified Region List
   struct ObjectRegionNotifieds {
      std::atomic<uint64_t> list;
      uint64_t Push(ObjectRegion region) {
         _INS_DEBUG(printf("PushNotifiedRegion\n"));
         _ASSERT(region->next.notified == none<sObjectRegion>());
         for (;;) {
            uint64_t current = this->list.load(std::memory_order_relaxed);
            uint64_t count = current & 0xffff;
            if (count < 1000) {
               count++;
            }
            else {
               //printf("Notified overflow\n");
            }
            uint64_t next = count | (uint64_t(region) << 16);
            region->next.notified = ObjectRegion(current >> 16);
            if (this->list.compare_exchange_weak(
               current, next,
               std::memory_order_release,
               std::memory_order_relaxed
            )) {
               return count;
            }
         }
      }
      ObjectRegion Flush() {
         uint64_t current = this->list.exchange(0);
         return ObjectRegion(current >> 16);
      }
      size_t Count() {
         uint64_t current = this->list.load(std::memory_order_relaxed);
         return current & 0xffff;
      }
   };

   /**********************************************************************
   *
   *   Object Analytics & Location
   *
   ***********************************************************************/

   struct ObjectAlivenessFlags {
      std::atomic_uint64_t flags;
   };

   struct ObjectAlivenessItem {
      uint16_t arenaID;
      std::atomic<uint32_t> next;
      std::atomic_uint64_t uncheckeds;
   };

   struct ObjectAnalysisSession {
      uint32_t* arenaIndexesMap = 0;

      ObjectAlivenessFlags* regionAlivenessMap = 0;
      ObjectAlivenessItem* regionItemsMap = 0;

      std::atomic<uint32_t> notifieds = 0;

      uint32_t allocated = 0;
      uint32_t length = 0;

      bool SetAlive(uint16_t arenaID, uint32_t regionIndex, uint64_t objectBit);
      void Postpone(uint16_t arenaID, uint32_t regionIndex, uint64_t objectBit);
      void Reset();
      void RunOnce();
      static void MarkPtr(void* ptr);

      static std::mutex running;
      static ObjectAnalysisSession* enabled;
   };

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
      ObjectLocation(address_t address) {
         this->arena = mem::Regions.ArenaMap[address.arenaID];
         this->layout = this->arena.layout(address.position >> this->arena.segmentation);
         if (this->layout.IsObjectRegion()) {
            auto& infos = mem::cst::ObjectLayoutBase[this->layout];
            auto offset = address.position & mem::cst::RegionMasks[this->arena.segmentation];
            this->index = infos.GetObjectIndex(offset);
            this->region = ObjectRegion(address.ptr - offset);
            this->object = ObjectHeader(uintptr_t(region) + infos.GetObjectOffset(this->index));
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
      size_t allocated_size() {
         if (this->object) {
            return this->region->GetObjectSize();
         }
         return 0;
      }
      size_t usable_size() {
         if (this->object) {
            auto size = this->region->GetObjectSize();
            if (this->object->hasAnalyticsInfos) {
               size -= sizeof(ObjectAnalyticsInfos);
            }
            if (this->object->hasSecurityPadding) {
               auto paddingEnd = size - sizeof(uint32_t);
               auto pBufferSize = (uint32_t*)&ObjectBytes(object)[paddingEnd];
               uint32_t bufferSize = (*pBufferSize) ^ 0xabababab;
               if (bufferSize < size) size = bufferSize;
            }
            return size;
         }
         return 0;
      }
      ObjectAnalyticsInfos* getAnalyticsInfos() {
         if (this->object && this->object->hasAnalyticsInfos) {
            auto size = this->region->GetObjectSize();
            auto pinfos = &ObjectBytes(object)[size - sizeof(ObjectAnalyticsInfos)];
            return (ObjectAnalyticsInfos*)pinfos;
         }
         return 0;
      }
      void* detectOverflowedBytes() {
         if (this->object && this->object->hasSecurityPadding) {
            auto size = this->region->GetObjectSize();
            if (this->object->hasAnalyticsInfos) {
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

   inline uint16_t getBlockDivider(uintptr_t block_size) {
      return ((uint64_t(1) << 16) + block_size - 1) / block_size;
   }

   inline uintptr_t applyBlockDivider(uint16_t divider, uintptr_t value) {
      return (uint64_t(value) * uint64_t(divider)) >> 16;
   }

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
