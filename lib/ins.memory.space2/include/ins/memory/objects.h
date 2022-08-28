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
   typedef struct sObjectChain* ObjectChain;
   typedef char* ObjectBytes;

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

   struct sObjectHeader {
      static const uint64_t cUnusedHeaderBits = 0;
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

   struct sObjectChain : sObjectHeader {
      sObjectChain* nextObject;
      sObjectChain* nextList;
      uint16_t length;
   };

#pragma pack(push,1)
   struct sObjectSlab {

      struct BlocksGC {
         uint32_t marks = 0; // Bit[k]: block k is gc marked entries
         uint32_t analyzis = 0; // Bit[k]: block k is gc analyzed entries
      };
      static_assert(sizeof(BlocksGC) == 8, "bad size");

      union {
         struct {
            std::atomic_uint32_t availables; // Bit[k]: block k is available
            uint8_t next;
            uint8_t __resv[3];
            BlocksGC gc;
         };
         uint64_t bits[2];
      };

      void Init() {
         this->bits[0] = 0;
         this->bits[1] = 0;
      }

      // Acquire block index
      index_t Acquire(uint16_t contextID) {
         index_t block_index = lsb_32(this->availables);
         auto block_bit = uint32_t(1) << block_index;
         _ASSERT((this->availables & block_bit) != 0);
         this->availables -= block_bit | 0x100000000;
         return block_index;
      }

      // Dispose: return true when context shall be in charge of the page
      bool Dispose(index_t block_index) {
         auto block_bit = uint32_t(1) << block_index;
         _ASSERT((this->availables & block_bit) == 0);
         auto prev = this->availables.fetch_add(block_bit | 0x100000000);
         return prev == 0;
      }
   };
   static_assert(sizeof(sObjectSlab) == cst::ObjectSlabSize, "bad size");
#pragma pack(pop)

   struct ObjectListBucket {
      ObjectChain reserve = 0;
      uint32_t listcount = 0;
      uint32_t maxObjectPerList = 32;
      uint32_t maxListCount = 2;
      ObjectHeader AcquireObject();
      ObjectChain DisposeObject(ObjectHeader obj);
      ObjectChain AcquireList();
      ObjectChain DisposeList(ObjectChain obj);
   };

   typedef struct sObjectRegion : sRegion {
      uint8_t layout = 0;
      ObjectListBucket bucket;
      std::mutex lock;
      ObjectRegion next = 0;
      void* owner = 0;
      uint8_t slab_availables = -1;

      size_t GetSize() override {
         return size_t(1) << ObjectLayoutInfos[layout].region_sizeL2;
      }
      ObjectHeader AcquireObject() {

         // Try alloc in context bucket
         auto obj = bucket.AcquireObject();
         if (obj) return obj;

         if (this->slab_availables != uint8_t(-1)) {
            auto slab_index = this->slab_availables;
            _ASSERT(slab_index < 255);

            // Pop slab
            auto& slab = this->GetSlabs()[slab_index];
            this->slab_availables = slab.next;
            slab.next = 0;

            // Cut slab into objects
            auto& infos = ObjectLayoutInfos[layout];
            auto slab_buffer = &ObjectBytes(this)[infos.object_base + slab_index * cst::ObjectPerSlab * infos.object_size];
            while (slab.availables) {
               auto index = lsb_32(slab.availables);
               auto obj = ObjectHeader(&slab_buffer[index * infos.object_size]);
               obj->bits = 0;
               slab.availables ^= uint32_t(1) << index;
               this->bucket.DisposeObject(obj);
            }
            return this->bucket.AcquireObject();
         }
         return 0;
      }
      ObjectChain AcquireList() {

         // Try alloc in context bucket
         auto lst = bucket.AcquireList();
         if (lst) return lst;

         return 0;
      }

      static sObjectRegion* New(uint8_t layout);

   protected:
      friend class sDescriptorAllocator;

      sObjectRegion(uint8_t layout)
         : layout(layout)
      {
         auto& infos = ObjectLayoutInfos[layout];
         auto lastIndex = infos.slab_count - 1;
         auto slabs = this->GetSlabs();
         for (int i = 0; i < lastIndex; i++) {
            auto& slab = slabs[i];
            slab.Init();
            slab.availables = 0xffffffff;
            slab.next = i + 1;
         }
         {
            auto& slab = slabs[lastIndex];
            slab.Init();
            slab.availables = (uint64_t(1) << (infos.object_count & cst::ObjectSlabMask)) - 1;
            slab.next = -1;
         }
         this->slab_availables = 0;
         printf("! new region: object_size=%d object_count=%d\n", infos.object_size, infos.object_count);
      }

      ObjectSlab GetSlabs() {
         return ObjectSlab(&ObjectBytes(this)[cst::ObjectRegionHeadSize]);
      }
   };
   static_assert(sizeof(sObjectRegion) <= cst::ObjectRegionHeadSize, "bad size");


   struct ObjectRegionBucket {
      ObjectRegion reserve = 0;
      void AddRegion(ObjectRegion region) {
         region->next = this->reserve;
         this->reserve = region;
      }
      ObjectHeader AcquireObject() {
         while (auto region = this->reserve) {
            if (auto obj = region->AcquireObject()) return obj;
            this->reserve = region->next;
            region->next = 0;
         }
         return 0;
      }
      ObjectChain Dispose(ObjectHeader obj);
      ObjectChain AcquireList() {
         while (auto region = this->reserve) {
            if (auto lst = region->AcquireList()) return lst;
            this->reserve = region->next;
            region->next = 0;
         }
         return 0;
      }
      ObjectChain DisposeList(ObjectChain obj);
   };

   struct ObjectClassHeap {
      uint8_t layout;
      ObjectRegionBucket regions;
      std::mutex lock;

      ObjectClassHeap(uint8_t layout)
         : layout(layout)
      {
      }

      ObjectChain AcquireList() {
         std::lock_guard<std::mutex> guard(lock);

         // Try alloc in regions bucket
         if (auto lst = regions.AcquireList()) return lst;

         // Acquire and alloc in a new region
         if (auto region = sObjectRegion::New(layout)) {
            region->owner = this;
            regions.AddRegion(region);
            return regions.AcquireList();
         }

         return 0;
      }
      void DiposeList(ObjectChain lst) {

      }
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
