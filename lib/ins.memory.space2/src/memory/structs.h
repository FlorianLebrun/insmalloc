#pragma once
#include <atomic>
#include <ins/binary/bitwise.h>

namespace ins {

   typedef size_t index_t;
   typedef void* ptr_t;

   //-------------------------------------------------------
   // Memory space constants
   //-------------------------------------------------------
   namespace cst {
      const size_t PageSizeL2 = 16;
      const size_t PageSize = size_t(1) << PageSizeL2;
      const uintptr_t PageMask = PageSize - 1;

      const size_t RegionSizeL2 = 24;
      const size_t RegionSize = size_t(1) << RegionSizeL2;
      const uintptr_t RegionMask = RegionSize - 1;

      const size_t SpaceSizeL2 = 40;
      const size_t SpaceSize = size_t(1) << SpaceSizeL2;
      const uintptr_t SpaceMask = SpaceSize - 1;

      const size_t RegionPerSpaceL2 = SpaceSizeL2 - RegionSizeL2;
      const size_t RegionPerSpace = size_t(1) << RegionPerSpaceL2;
      const size_t PagePerRegionL2 = RegionSizeL2 - PageSizeL2;
      const size_t PagePerRegion = size_t(1) << PagePerRegionL2;

      const size_t PackingCount = 4;
   }

   //-------------------------------------------------------
   // Memory address representation
   //-------------------------------------------------------
#pragma pack(push,1)
   union address_t {
      uintptr_t ptr;
      struct {
         uint64_t position : 16;
         uint64_t pageID : 10;
         uint64_t regionID : 38;
      };
      struct {
         uint64_t position : 16;
         uint64_t pageIndex : 48;
      };
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
      index_t divide(index_t divided) {
         return divided / this->value();
      }
   };
   

   struct exception_missing_memory : std::exception {

   };
}

