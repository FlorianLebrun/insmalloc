#pragma once
#include <atomic>
#include <exception>
#include <functional>
#include <ins/macros.h>
#include <ins/binary/bitwise.h>
#include <stdio.h>

namespace ins::mem {

   typedef size_t index_t;
   typedef void* ptr_t;
   typedef uint8_t* BufferBytes;

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
      const size_t RegionSizingCount = 33;

      const size_t PagePerArenaL2 = ArenaSizeL2 - PageSizeL2;
      const size_t ArenaPerSpaceL2 = SpaceSizeL2 - ArenaSizeL2;
      const size_t ArenaPerSpace = size_t(1) << ArenaPerSpaceL2;
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
      address_t(uint32_t arenaID, uint32_t position) : arenaID(arenaID), position(position) {}
      operator uintptr_t() { return ptr; }
      operator void* () { return (void*)ptr; }
      void operator = (uintptr_t ptr) { this->ptr = ptr; }
      template<typename T>
      T* as() { return (T*)ptr; }
   };
   static_assert(sizeof(address_t) == 8, "bad size");
#pragma pack(pop)

   struct sz2a {
      sz2a(size_t size);
      void set(size_t size, size_t factor, const char* unit);
      operator char* () { return this->chars; }
      const char* c_str() { return this->chars; }
      char chars[32];
   };

   struct exception_missing_memory : std::exception {
      // memory starving
   };

   template<typename T>
   static constexpr T* none() {
      return (T*)-1;
   }

}
