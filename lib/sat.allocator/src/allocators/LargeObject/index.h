#pragma once
#include "../../heaps/heaps.h"

namespace LargeObjectAllocator {

   class LargeObjectSegment :public sat::MemorySegmentController {
   public:
      uint8_t heapID;
      uint32_t index;
      uint32_t length;
      uint64_t meta;
      LargeObjectSegment(uint8_t heapID, uint32_t index, uint32_t length, uint64_t meta);
      virtual const char* getName() override;
      virtual int free(uintptr_t index, uintptr_t ptr) override;
      virtual bool getAddressInfos(uintptr_t index, uintptr_t ptr, sat::tpObjectInfos infos) override;
      virtual int traverseObjects(uintptr_t index, sat::IObjectVisitor* visitor) override;
   };

   struct Global : public sat::ObjectAllocator {
      int heapID;

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

   bool get_address_infos(uintptr_t ptr, sat::tpObjectInfos infos);
}
