#pragma once
#include <atomic>
#include <exception>
#include <ins/macros.h>
#include <ins/binary/bitwise.h>

namespace ins {

   typedef size_t index_t;
   typedef void* ptr_t;

   //-------------------------------------------------------
   // Memory space constants
   //-------------------------------------------------------
   namespace cst {
      // Memory hierarchy:
      // Space <- Arena <- Region <- Page

      // Space: all accessible virtual memory
      const size_t SpaceSizeL2 = 48;
      const size_t SpaceSize = size_t(1) << SpaceSizeL2;
      const uintptr_t SpaceMask = SpaceSize - 1;

      // Page: physical memory mapping granularity
      const size_t PageSizeL2 = 16;
      const size_t PageSize = size_t(1) << PageSizeL2;
      const uintptr_t PageMask = PageSize - 1;

      // Arena: managed memory zone specialized to one region segmentation
      const size_t ArenaSizeL2 = 32;
      const size_t ArenaSize = size_t(1) << ArenaSizeL2;
      const uintptr_t ArenaMask = ArenaSize - 1;

      // Region: memory zone with defined usage, and defining a page mapping policy
      const size_t RegionSizeMinL2 = PageSizeL2;
      const size_t RegionSizeMin = size_t(1) << RegionSizeMinL2;
      const size_t RegionSizeMaxL2 = ArenaSizeL2;
      const size_t RegionSizeMax = size_t(1) << RegionSizeMaxL2;

      const size_t PagePerArenaL2 = ArenaSizeL2 - PageSizeL2;
      const size_t ArenaPerSpaceL2 = SpaceSizeL2 - ArenaSizeL2;
      const size_t ArenaPerSpace = size_t(1) << ArenaPerSpaceL2;

      const size_t PackingCount = 4;
   }

   //-------------------------------------------------------
   // Memory address representation
   //-------------------------------------------------------
#pragma pack(push,1)
   union address_t {
      uintptr_t ptr;
      struct {
         uint64_t byteID : cst::PageSizeL2;
         uint64_t pageID : cst::PagePerArenaL2;
         uint64_t arenaID : 32;
      };
      struct {
         uint64_t position : 32;
      };
      address_t() : ptr(0) {}
      address_t(uintptr_t ptr) : ptr(ptr) {}
      address_t(void* ptr) : ptr(uintptr_t(ptr)) {}
      operator uintptr_t() { return ptr; }
      operator void* () { return (void*)ptr; }
      template<typename T>
      T* as() { return (T*)ptr; }
   };
   static_assert(sizeof(address_t) == 8, "bad size");
#pragma pack(pop)

   //-------------------------------------------------------
   // Memory size representation
   //-------------------------------------------------------

   // Compact size def
   union sizeid_t {
      uint8_t bits;
      struct {
         uint8_t packing : 3; // in { 1, 3, 5, 7 }
         uint8_t shift : 5;
      };
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
      index_t multiply(index_t value) {
         return (value << this->shift) * this->packing;
      }
      sizeid_t id() {
         return sizeid_t(packing, shift);
      }
   };


   struct exception_missing_memory : std::exception {

   };

   template<typename T>
   static constexpr T* none() {
      return (T*)-1;
   }

}
