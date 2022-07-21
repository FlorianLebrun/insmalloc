#include "./descriptors.h"
#include <stdint.h>
#include <stdlib.h>
#include <semaphore>
#include <vector>
#include "../os/memory.h"

using namespace ins;

void test_descriptor_region() {

   uintptr_t region_sizeL2 = cst::RegionSizeL2 + 0;
   uintptr_t region_buf = OSMemory::ReserveMemory(0, 0, size_t(1) << region_sizeL2, cst::RegionSize);
   OSMemory::CommitMemory(region_buf, cst::PageSize);
   sDescriptorRegion* region = new((void*)region_buf) sDescriptorRegion(region_sizeL2);

   struct TestExtDesc : sExtensibleDescriptor {
      sDescriptorRegion* region;
      size_t commited;
      size_t size;

      TestExtDesc(size_t commited, size_t size, sDescriptorRegion* region) : size(size), commited(commited), region(region) {
         for (int i = sizeof(*this); i < commited; i++) ((char*)this)[i] = 0;
         region->Extends(this, size);
         for (int i = sizeof(*this); i < size; i++) ((char*)this)[i] = 0;
      }
      virtual size_t GetSize() {
         return this->size;
      }
      virtual void SetUsedSize(size_t commited) {
         this->commited = commited;
      }
      virtual size_t GetUsedSize() {
         return this->commited;
      }
   };
   auto desc = region->NewExtensible<TestExtDesc>(1000000, region);
   region->Dispose(desc);

   struct TestDesc : sDescriptor {
      virtual size_t GetSize() {
         return sizeof(*this);
      }
   };
   std::vector<Descriptor> descs;
   for (int i = 0; i < 10000; i++) {
      auto desc = region->New<TestDesc>();
      descs.push_back(desc);
   }
   for (auto desc : descs) {
      region->Dispose(desc);
   }
}
