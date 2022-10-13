#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <mutex>
#include <ins/binary/alignment.h>
#include <ins/binary/bitwise.h>
#include <ins/memory/structs.h>
#include <ins/memory/os-memory.h>
#include <ins/avl/AVLOperators.h>

namespace ins {

   struct Descriptor;
   struct ArenaDescriptor;

   enum class DescriptorType {
      Free,
      System,
      Region,
      Arena,
   };

   struct Descriptor {
      virtual size_t GetSize() = 0;
      virtual DescriptorType GetType() { return DescriptorType::System; }
      virtual void SetUsedSize(size_t commited) { throw "not extensible"; }
      virtual size_t GetUsedSize() { return this->GetSize(); }
      virtual void Resize(size_t newSize);
      virtual void Dispose();

      template<typename T, typename ...Targ>
      static T* New(Targ... args) {
         auto sizeL2 = GetBlockSizeL2(sizeof(T));
         auto result = new(AllocateDescriptor(sizeL2, sizeL2)) T(args...);
         _ASSERT(!result || result->GetSize() == sizeof(T));
         return result;
      }
      template<typename T, typename ...Targ>
      static T* NewBuffer(size_t size, Targ... args) {
         _ASSERT(size >= sizeof(T));
         auto sizeL2 = GetBlockSizeL2(size);
         auto result = new(AllocateDescriptor(sizeL2, sizeL2)) T(args...);
         _ASSERT(!result || result->GetSize() == size);
         return result;
      }
      template<typename T, typename ...Targ>
      static T* NewExtensible(size_t size, Targ... args) {
         auto sizeL2 = GetBlockSizeL2(size);

         size_t usedSizeL2 = 0;
         if (sizeL2 <= cst::PageSizeL2) usedSizeL2 = sizeL2;
         else if (sizeof(T) >= cst::PageSize) usedSizeL2 = GetBlockSizeL2(sizeof(T));
         else usedSizeL2 = cst::PageSizeL2;

         auto result = new(AllocateDescriptor(sizeL2, usedSizeL2)) T(size_t(1) << usedSizeL2, size_t(1) << sizeL2, args...);
         _ASSERT(!result || result->GetSize() >= size);
         _ASSERT(!result || result->GetUsedSize() >= (1 << usedSizeL2));
         return result;
      }
   protected:
      ~Descriptor();
      static Descriptor* AllocateDescriptor(size_t sizeL2, size_t usedSizeL2 = 0);
      static size_t GetBlockSizeL2(size_t size);
   };

   union RegionEntry {
      static const uint8_t cFreeBits = 0x00;
      static const uint8_t cOpaqueBits = 0x80;
      static const uint8_t cDescriptorBits = 0x80|0x40;
      uint8_t bits;
      struct {
         uint8_t __resv : 6;
         uint8_t hasDescriptor : 1; // when hasDescriptor = 1: region is 'sRegion'
         uint8_t hasNoObjects : 1; // when hasNoObjects = 1: region is not 'sObjectRegion'
      };
      struct {
         // When: sObjectRegionDescriptor
         uint8_t objectLayoutID : 7; // object sizing of the region
      };
      RegionEntry(uint8_t bits = 0)
         : bits(bits) {
      }
      bool IsFree() {
         return this->bits == cFreeBits;
      }
      bool IsObjectRegion() {
         return !this->hasNoObjects;
      }
      static RegionEntry ObjectRegion(uint8_t objectLayoutID) {
         _ASSERT(objectLayoutID < 128);
         return RegionEntry(objectLayoutID);
      }
   };
   static_assert(sizeof(RegionEntry) == 1, "bad size");

   struct ArenaDescriptor : Descriptor {
      address_t base;
      uint8_t sizing;
      std::atomic_uint32_t availables_count = 0;
      ArenaDescriptor* next = 0;
      RegionEntry regions[1];
      ArenaDescriptor(uintptr_t base, uint8_t sizing);
      size_t GetSize() override {
         return GetSize(this->sizing);
      }
      DescriptorType GetType() override {
         return DescriptorType::Arena;
      }
      size_t GetRegionCount() {
         return size_t(1) << (cst::ArenaSizeL2 - this->sizing);
      }
      static size_t GetSize(uint8_t sizing) {
         return sizeof(ArenaDescriptor) + (cst::ArenaSize >> sizing) * sizeof(RegionEntry);
      }
   };

   union ArenaEntry {
      uint64_t bits;
      struct {
         uint64_t segmentation : 8;
         uint64_t reference : 56;
      };
      ArenaEntry()
         : bits(0) {
      }
      ArenaEntry(ArenaDescriptor* descriptor)
         : reference(uint64_t(descriptor)), segmentation(descriptor->sizing) {
      }
      ArenaDescriptor* descriptor() {
         return (ArenaDescriptor*)this->reference;
      }
      operator bool() {
         return this->bits != 0;
      }
   };
   static_assert(sizeof(ArenaEntry) == 8, "bad size");

}