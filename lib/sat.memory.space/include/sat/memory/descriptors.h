#pragma once
#include <atomic>
#include <sat/binary/bitwise.h>

namespace sat {

   struct MemoryContext;

   //-------------------------------------------------------
   // Memory space constants
   //-------------------------------------------------------
   const size_t cPageSizeL2 = 16;
   const size_t cPageSize = size_t(1) << cPageSizeL2;
   const uintptr_t cPageMask = cPageSize - 1;

   const size_t cUnitSizeL2 = 22;
   const size_t cUnitSize = size_t(1) << cUnitSizeL2;
   const uintptr_t cUnitMask = cUnitSize - 1;

   const size_t cRegionSizeL2 = 32;
   const size_t cRegionSize = size_t(1) << cRegionSizeL2;
   const uintptr_t cRegionMask = cRegionSize - 1;

   const size_t cRegionPerSpaceL2 = 8;
   const size_t cRegionPerSpace = size_t(1) << cRegionPerSpaceL2;
   const size_t cUnitPerRegionL2 = 10;
   const size_t cUnitPerRegion = size_t(1) << cUnitPerRegionL2;
   const size_t cPagePerUnitL2 = 6;
   const size_t cPagePerUnit = size_t(1) << cPagePerUnitL2;
   const size_t cPagePerRegionL2 = cRegionSizeL2 - cPageSizeL2;
   const size_t cPagePerRegion = size_t(1) << cPagePerRegionL2;

   //-------------------------------------------------------
   // Memory address representation
   //-------------------------------------------------------
#pragma pack(push,1)
   union address_t {
      uintptr_t ptr;
      struct {
         uint16_t position;
         uint16_t pageID;
         uint32_t regionID;
      };
      struct {
         uint16_t position;
         uint32_t pageIndex;
      };
      struct Unit {
         uint16_t position;
         uint16_t pageID : 6;
         uint16_t unitID : 10;
         uint32_t regionID;
      } unit;
      address_t() : ptr(0) {}
      address_t(uintptr_t ptr) : ptr(ptr) {}
      address_t(void* ptr) : ptr(uintptr_t(ptr)) {}
      operator uintptr_t() { return ptr; }
      operator void* () { return (void*)ptr; }
   };
   static_assert(sizeof(address_t) == 8, "bad size");
#pragma pack(pop)

   //-------------------------------------------------------
   // Memory size representation
   //-------------------------------------------------------
   const size_t cPackingCount = 4;

   // Compact size def
   struct sizeid_t {
      uint8_t packing : 3; // in { 1, 3, 5, 7 }
      uint8_t shift : 5;
      sizeid_t(uint8_t packing = 0, uint8_t shift = 0)
         : packing(packing), shift(shift) {
      }
      size_t size() {
         return size_t(packing) << shift;
      }
   };

   // Normal size def
   struct size_target_t {
      uint16_t packing;
      uint16_t shift;
      size_target_t(size_t size) {
         size_t exp, base;
         if (size > 8) {
            exp = size_t(msb_32(size)) - 2;
            base = size >> exp;
         }
         else {
            exp = 0;
            if (!(base = size)) return;
         }
         if (size != (base << exp)) base++;
         while ((base & 1) == 0) { exp++; base >>= 1; }
         this->packing = base;
         this->shift = exp;
         _ASSERT(size > this->lower() && size <= this->value());
      }
      size_t value() {
         return size_t(this->packing) << this->shift;
      }
      size_t lower() {
         if (this->shift > 3) {
            if (this->packing > 1) return (this->packing - size_t(1)) << this->shift;
            else return size_t(7) << (this->shift - 3);
         }
         else return 0;
      }
   };

   //-------------------------------------------------------
   // Memory space descriptors
   //-------------------------------------------------------
   struct PageEntry {
      uint64_t layoutID : 8;
      uint64_t reference : 56;
   };
   static_assert(sizeof(PageEntry) == 8, "bad size");

#pragma pack(push,1)
   typedef struct PageDescriptor {
      uint64_t uses; // Bitmap of allocated entries
      uint64_t usables; // Bitmap of allocable entries
      uint64_t gc_marks; // Bitmap of gc marked entries
      uint64_t gc_analyzis; // Bitmap of gc analyzed entries
      uint32_t page_index; // Absolute page index
      uint8_t class_id;
   } *tpPageDescriptor;
#pragma pack(pop)

   // Page: is a memory zone of one block or paved with slab
#pragma pack(push,1)
   typedef struct SlabBatchDescriptor : PageDescriptor {
      uint8_t length;
      SlabBatchDescriptor* next;

      uint8_t __reserve[18];
   } *tpSlabBatchDescriptor;
   static_assert(sizeof(SlabBatchDescriptor) == 64, "bad size");
#pragma pack(pop)

   // Slab: is a memory zone paved with same size blocks
#pragma pack(push,1)
   typedef struct SlabDescriptor : PageDescriptor {
      uint8_t context_id;
      uint8_t slab_position; // Slab position in a desriptors array, 0 when slab is alone
      uint8_t block_ratio_shift; // Slab index in a desriptors array, 0 when slab is alone
      union {
         uint8_t needAnalysis : 1;
      };
      uint8_t __reserve[7];
      std::atomic_uint64_t shared_freemap;
      SlabDescriptor* next; // Chaining slot to link slab in a slab queue

   } *tpSlabDescriptor;
   static_assert(sizeof(SlabDescriptor) == 64, "bad size");
#pragma pack(pop)

   //-------------------------------------------------------
   // Memory slicing model
   //-------------------------------------------------------
   struct PageLayout {
      uint32_t offset;
      uint32_t scale;
   };

   class ElementClass {
   public:
      uint8_t id = -1;
      ElementClass(uint8_t id);
   };

   class SlabClass : public ElementClass {
   public:
      SlabClass(uint8_t id);
      virtual tpSlabDescriptor allocate(MemoryContext* context) = 0;
      virtual void release(tpSlabDescriptor slab, MemoryContext* context) = 0;
      virtual sizeid_t getSlabSize() = 0;
   };

   class BlockClass : public ElementClass {
   public:
      BlockClass(uint8_t id);
      virtual address_t allocate(size_t target, MemoryContext* context) = 0;
      virtual void receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) = 0;
      virtual SlabClass* getSlabClass() = 0;
      virtual size_t getSizeMax() = 0;
      virtual sizeid_t getBlockSize() { throw; }
      virtual void print() = 0;
   };

}

