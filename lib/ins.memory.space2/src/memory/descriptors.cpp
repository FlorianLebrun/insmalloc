#include <ins/memory/descriptors.h>
#include <ins/memory/space.h>
#include <vector>

using namespace ins;

void ins::sDescriptor::Dispose() {
   auto region = (sDescriptorAllocator*)space.GetRegionDescriptor(this);
   region->Dispose(this);
}

void ins::sDescriptor::Resize(size_t newSize) {
   auto region = (sDescriptorAllocator*)space.GetRegionDescriptor(this);
   region->Extends(this, newSize);
}

sDescriptorAllocator* ins::sDescriptorAllocator::New(size_t countL2) {
   size_t count = size_t(1) << countL2;
   address_t buffer = space.ReserveArena();
   OSMemory::CommitMemory(buffer, cst::PageSize);
   auto region = new((void*)buffer) sDescriptorAllocator(countL2);
   space.arenas[buffer.arenaID] = ArenaEntry(&region->arena);
   space.SetRegionEntry(region, RegionEntry::cDescriptorBits);
   return region;
}

ins::sArenaDescriptor::sArenaDescriptor(uint8_t sizing) {
   this->sizing = sizing;
   this->availables_count = cst::ArenaSize >> sizing;
   memset(this->regions,RegionEntry::cFreeBits, this->availables_count);
}

void test_descriptor_region() {

   auto region = sDescriptorAllocator::New(0);

   struct TestExtDesc : sDescriptor {
      size_t commited;
      size_t size;

      TestExtDesc(size_t commited, size_t size) : size(size), commited(commited) {
         for (int i = sizeof(*this); i < commited; i++) ((char*)this)[i] = 0;
         this->Resize(size);
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
   auto desc = region->NewExtensible<TestExtDesc>(1000000);
   desc->Dispose();

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
      desc->Dispose();
   }
}
