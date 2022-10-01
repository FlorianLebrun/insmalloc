#pragma once
#include <ins/memory/descriptors.h>
#include <ins/memory/structs.h>
#include <ins/memory/objects-config.h>
#include <stdint.h>
#include <stdlib.h>

namespace ins {

   typedef struct sObjectRegion* ObjectRegion;
   typedef struct sObjectSlab* ObjectSlab;
   typedef struct sObjectHeader* ObjectHeader;
   typedef char* ObjectBytes;
   struct ObjectClassHeap;

   namespace cst {
      const size_t ObjectPerSlabL2 = 5;
      const size_t ObjectPerSlab = size_t(1) << ObjectPerSlabL2;
      const size_t ObjectSlabMask = ObjectPerSlab - 1;

      const size_t ObjectSlabSize = 16;
      const size_t ObjectSlabOffset = 16;
      const size_t ObjectRegionHeadSize = ObjectSlabOffset * ObjectSlabSize;
   }

   enum ObjectLayoutPolicy {
      SmallSlabPolicy, // slab size is less than a page
      MediumSlabPolicy, // slab size is greater than a page
      LargeSlabPolicy, // object size is less than a page
      HugeSlabPolicy, // object size is greater than a page
   };

   // Object size fixed point divider: index = (uint64_t(position)*ObjectDividerFixed32[clsID]) >> 32
   struct tObjectLayoutInfos {
      uint32_t object_base;
      uint32_t object_divider;
      uint32_t object_size;
      uint16_t object_count;
      uint8_t slab_count;
      uint8_t region_sizeL2;
      ObjectLayoutPolicy policy;
   };

   extern const size_t ObjectLayoutCount;
   extern const tObjectLayoutInfos ObjectLayoutInfos[];
   extern const uint32_t ObjectLayoutDividerFixed32[];

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
            uint8_t used : 1; // when 0 object is in reserve, not really allocated
            uint8_t hasAnaliticsTrace : 1; // define that object is followed by analytics structure
            uint8_t hasSecurityPadding : 1; // define that object is followed by a padding of security(canary)

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

      typedef struct sObjectChain : sObjectHeader {
         sObjectChain* nextObject;
         uint64_t nextList : 48;
         uint64_t length : 16;
      } *ObjectChain;
      static_assert(sizeof(sObjectChain) == sizeof(uint64_t) * 3, "bad size");

      ObjectChain items = 0;
      uint32_t count = 0;
      uint16_t list_count = 0;
      uint16_t list_length = 32;

      void PushObject(ObjectHeader obj);
      ObjectHeader PopObject();

      bool TransfertBatch(ObjectBucket& receiver);
   };

   /**********************************************************************
   *
   *   Functions
   *
   ***********************************************************************/
   struct IObjectRegionOwner {
      virtual void NotifyAvailableRegion(sObjectRegion* region) = 0;
   };

   inline uint16_t getBlockDivider(uintptr_t block_size) {
      return ((uint64_t(1) << 16) + block_size - 1) / block_size;
   }

   inline uintptr_t applyBlockDivider(uint16_t divider, uintptr_t value) {
      return (uint64_t(value) * uint64_t(divider)) >> 16;
   }

   inline uintptr_t getBlockIndexToOffset(uintptr_t block_size, uintptr_t block_index, uintptr_t region_mask) {
      return (region_mask - block_size + 1) - block_index * block_size;
   }

   inline uintptr_t getBlockOffsetToIndex(uintptr_t block_divider, uintptr_t block_offset, uintptr_t region_mask) {
      return (uint64_t((region_mask - block_offset) & region_mask) * block_divider) >> 32;
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
         for (auto layout = bin.layoutMin; layout <= bin.layoutMax; layout++) {
            if (size <= ObjectLayoutInfos[layout].object_size) return layout;
         }
         _ASSERT(0);
      }
      else if (size < LargeSizeLimit) {
         const size_t size_step = LargeSizeLimit / LayoutRangeSizeCount;
         auto& bin = large_object_layouts[size / size_step];
         for (auto layout = bin.layoutMin; layout <= bin.layoutMax; layout++) {
            if (size <= ObjectLayoutInfos[layout].object_size) return layout;
         }
         _ASSERT(0);
      }
      return ObjectLayoutCount - 1;
   };

}
