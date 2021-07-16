#pragma once
#include "../../heaps/heaps.h"

namespace BuddyAllocator {

   struct BuddyObject;
   struct BuddyFreeObject;

   struct BuddyObject {
      sat::tObjectMetaData meta;
      bool isFree() {
         return this->meta.typeID == -1;
      }
   };

   struct BuddyFreeObject : BuddyObject {
      BuddyFreeObject* next;
      int classIndex;
   };

   struct BuddyClassSize {
      BuddyFreeObject* freelist;
      sat::SpinLock lock;
   };

   /*
   - classes : 64   128 256 512 1k 2k 4k 8k
   - map size: 1024 512 256 128 64 32 16 8  = 2040 bits = 255 bytes
   */
   class BuddySegmentController : public sat::MemorySegmentController {
   public:
      uint8_t heapID;
      BuddyClassSize classSizes[8];

      virtual const char* getName() override;
      virtual int free(uintptr_t index, uintptr_t ptr) override;
      virtual bool getAddressInfos(uintptr_t index, uintptr_t ptr, sat::tpObjectInfos infos) override;
      virtual int traverseObjects(uintptr_t index, sat::IObjectVisitor* visitor) override;
      virtual int getHeapID() override { return this->heapID; }
   };


   struct Global : public sat::ObjectAllocator {
      BuddySegmentController controller;

      void init(sat::Heap* pageHeap);

      virtual size_t getMaxAllocatedSize() override;
      virtual size_t getMinAllocatedSize() override;
      virtual size_t getAllocatedSize(size_t size) override;
      virtual void* allocate(size_t size) override;

      virtual size_t getMaxAllocatedSizeWithMeta() override;
      virtual size_t getMinAllocatedSizeWithMeta() override;
      virtual size_t getAllocatedSizeWithMeta(size_t size) override;
      virtual void* allocateWithMeta(size_t size, uint64_t meta) override;
   };
}
