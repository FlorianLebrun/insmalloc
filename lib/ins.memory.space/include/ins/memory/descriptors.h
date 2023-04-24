#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <mutex>
#include <functional>
#include <ins/binary/alignment.h>
#include <ins/binary/bitwise.h>
#include <ins/memory/structs.h>
#include <ins/avl/AVLOperators.h>

namespace ins::mem {
   struct DescriptorTypeID {
      enum {
         Undefined = 0,
         FreeBlock = 1,
         FreeSpan = 2,
         DescriptorHeap = 3,
         Arena = 4,
         ObjectRegion = 5,
      };
   };

   struct DescriptorType {
      uint8_t typeID = 0;
      const char* name[128] = { 0 };

      static DescriptorType types[64];
   };

   typedef struct sDescriptorEntry {
      uint8_t typeID;
      uint8_t sizeL2;
      uint8_t usedSizeL2;
      uint8_t __resv[5];
      BufferBytes GetBuffer();
      static size_t GetBlockSizeL2(size_t size);
      static size_t GetBufferSizeL2(size_t size);
   } *DescriptorEntry;
   static_assert(sizeof(sDescriptorEntry) == sizeof(uint64_t), "bad size");

   struct Descriptor {
      Descriptor();
      Descriptor(uint8_t typeID);
      DescriptorEntry GetEntry();
      size_t GetSize();
      uint8_t GetType();
      size_t GetUsedSize();
      void Resize(size_t newSize);

      template<typename T, typename ...Targ>
      static T* New(Targ... args) {
         return new(AllocateDescriptor(sizeof(T), sizeof(T))) T(args...);
      }
      template<typename T, typename ...Targ>
      static T* NewBuffer(size_t size, Targ... args) {
         _ASSERT(size >= sizeof(T));
         return new(AllocateDescriptor(size, size)) T(args...);
      }
      template<typename T, typename ...Targ>
      static T* NewExtensible(size_t size, Targ... args) {
         return new(AllocateDescriptor(size, sizeof(T))) T(args...);
      }
      template<typename T>
      static void Delete(T* obj) {
         struct Tx : T, Descriptor {};
         delete (Tx*)obj;
      }
      static void operator delete(void*);
   protected:
      static BufferBytes AllocateDescriptor(size_t size, size_t usedSize = 0);
   };
}