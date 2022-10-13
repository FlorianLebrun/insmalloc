#pragma once
#include <ins/memory/descriptors.h>
#include <ins/memory/structs.h>
#include <ins/memory/objects-config.h>
#include <stdint.h>
#include <stdlib.h>

namespace ins {

   typedef struct sObjectSlab* ObjectSlab;
   typedef struct sObjectHeader* ObjectHeader;
   typedef struct sObjectRegion* ObjectRegion;
   typedef uint8_t* ObjectBytes;

   struct IObjectRegionOwner;

   namespace cst {
      const size_t ObjectPerSlabL2 = 5;
      const size_t ObjectPerSlab = size_t(1) << ObjectPerSlabL2;
      const size_t ObjectSlabMask = ObjectPerSlab - 1;

      const size_t ObjectSlabSize = 16;
      const size_t ObjectSlabOffset = 16;
      const size_t ObjectRegionHeadSize = ObjectSlabOffset * ObjectSlabSize;
   }

   enum ObjectLayoutPolicy {
      SlabbedObjectPolicy, // region with multiple objects
      LargeObjectPolicy, // region with one object
      UncachedLargeObjectPolicy, // region with one object and no management policy
   };

   // Object size fixed point divider: index = (uint64_t(position)*ObjectDividerFixed32[clsID]) >> 32 
   struct tObjectLayoutBase {
      uint32_t object_base;
      uint32_t object_divider;
      uint32_t object_size;
      uint32_t region_mask;
      uintptr_t GetObjectIndex(uintptr_t offset) const {
         return ((offset - object_base) * object_divider) >> 16;
      }
      uintptr_t GetObjectOffset(uintptr_t index) const {
         return object_base + index * object_size;
      }
   };

   struct tObjectLayoutInfos {
      uint32_t object_base;
      uint32_t object_divider;
      uint32_t object_size;
      uint16_t object_count;
      uint8_t slab_count;
      uint8_t region_sizeL2;
      uint32_t region_position_mask;
      ObjectLayoutPolicy policy;
      uintptr_t GetObjectIndex(uintptr_t offset) const {
         return ((offset - object_base) * object_divider) >> 16;
      }
      uintptr_t GetObjectOffset(uintptr_t index) const {
         return object_base + index * object_size;
      }
   };

   extern const size_t ObjectLayoutCount;
   extern const tObjectLayoutInfos ObjectLayoutInfos[];
   extern const tObjectLayoutBase ObjectLayoutBase[];

   /**********************************************************************
   *
   *   Object Header
   *
   ***********************************************************************/
   struct sObjectHeader {
      static const uint64_t cNewHeaderBits = 1;
      static const uint64_t cDisposedHeaderBits = 0;
      static const uint64_t cDeadHeaderBits = -1;
      union {
         struct {
            // Not thread protected bits (changed only on alloc/free, ie. when only one thread point to it)
            uint32_t used : 1; // when 0 object is in reserve, not really allocated
            uint32_t hasAnalyticsInfos : 1; // define that object is followed by analytics structure
            uint32_t hasSecurityPadding : 1; // define that object is followed by a padding of security(canary)
            uint32_t schemaID : 24;

            // Thread protected bits
            uint32_t retentionCounter : 8; // define how many times the object shall be dispose to be really deleted
            uint32_t weakRetentionCounter : 6; // define how many times the object shall be unlocked before the memory is reusable
            uint32_t lock : 1; // general multithread protection for this object
         };
         uint64_t bits;
      };
   };
   static_assert(sizeof(sObjectHeader) == sizeof(uint64_t), "bad size");

#pragma pack(push,1)
   struct sObjectSlab {

      union {
         struct {
            std::atomic_uint32_t availables; // Bit[k]: block k is available
            uint32_t marks; // Bit[k]: block k is gc marked entries
            uint32_t analyzis; // Bit[k]: block k is gc analyzed entries
            uint8_t __resv[4];
         };
         uint64_t bits[2];
      };

      void Init() {
         this->bits[0] = 0;
         this->bits[1] = 0;
      }

   };
   static_assert(sizeof(sObjectSlab) == cst::ObjectSlabSize, "bad size");
#pragma pack(pop)

   /**********************************************************************
   *
   *   Object Bucket
   *
   ***********************************************************************/
   struct ObjectBucket {
   private:
      typedef struct sObjectChain : sObjectHeader {
         sObjectChain* nextObject;
         uint64_t nextBatch : 48;
         uint64_t length : 16;
      } *ObjectChain;
      static_assert(sizeof(sObjectChain) == sizeof(uint64_t) * 3, "bad size");

      ObjectChain items = 0;
      ObjectChain batches = 0;
   public:
      uint32_t count = 0;
      uint16_t batch_count = 0;
      uint16_t batch_length = 32;

      void PushObject(ObjectHeader obj);
      ObjectHeader PopObject();

      bool TransfertBatchInto(ObjectBucket& receiver);
      void DumpInto(ObjectBucket& receiver);

      void CheckValidity();
   };

   /**********************************************************************
   *
   *   Object Region
   *
   ***********************************************************************/
   struct sObjectRegion : Descriptor {
      IObjectRegionOwner* owner = 0;
      const tObjectLayoutInfos& infos;
      sObjectRegion(const tObjectLayoutInfos& infos) : infos(infos) {}
      virtual void DisplayToConsole() = 0;
   };

   // Object Region Owner interface
   struct IObjectRegionOwner {
      virtual void NotifyAvailableRegion(sObjectRegion* region) = 0;
   };

   // Object Region List
   template <typename sObjectRegion>
   struct ObjectRegionList {
      typedef sObjectRegion* ObjectRegion;
      ObjectRegion first = 0;
      ObjectRegion last = 0;
      uint32_t count = 0;

      void PushRegion(ObjectRegion region) {
         region->next.used = 0;
         if (!this->last) this->first = region;
         else this->last->next.used = region;
         this->last = region;
         this->count++;
      }
      ObjectRegion PopRegion() {
         if (auto region = this->first) {
            this->first = region->next.used;
            if (!this->first) this->last = 0;
            this->count--;
            region->next.used = none<sSlabbedObjectRegion>();
            return region;
         }
         return 0;
      }
      void DumpInto(ObjectRegionList& receiver, IObjectRegionOwner* owner) {
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
      if (size < SmallSizeLimit) {
         const size_t size_step = SmallSizeLimit / LayoutRangeSizeCount;
         auto index = (size + 7) / size_step;
         return small_object_layouts[index];
      }
      else if (size < MediumSizeLimit) {
         const size_t size_step = MediumSizeLimit / LayoutRangeSizeCount;
         auto& bin = medium_object_layouts[size / size_step];
         if (size <= ObjectLayoutInfos[bin.layoutMin].object_size) return bin.layoutMin;
         else return bin.layoutMax;
         _ASSERT(0);
      }
      else if (size < LargeSizeLimit) {
         const size_t size_step = LargeSizeLimit / LayoutRangeSizeCount;
         auto& bin = large_object_layouts[size / size_step];
         if (size <= ObjectLayoutInfos[bin.layoutMin].object_size) return bin.layoutMin;
         else return bin.layoutMax;
         _ASSERT(0);
      }
      return ObjectLayoutCount - 1;
   };

}
