#include <ins/memory/map.h>
#include <ins/memory/file-view.h>
#include <stdio.h>
#include <vector>
#include <unordered_map>

using namespace ins;
using namespace ins::mem;

namespace DescriptorsTests {
   void test_descriptor_region() {

      struct TestExtDesc : Descriptor {
         TestExtDesc() {
            size_t commited = this->GetUsedSize();
            size_t size = this->GetSize();
            for (int i = sizeof(*this); i < commited; i++) ((char*)this)[i] = 0;
            this->Resize(size);
            for (int i = sizeof(*this); i < size; i++) ((char*)this)[i] = 0;
         }
      };
      auto desc = Descriptor::NewExtensible<TestExtDesc>(1000000);
      delete desc;

      struct TestDesc : Descriptor {
      };

      std::vector<Descriptor*> descs;
      for (int i = 0; i < 10000; i++) {
         auto desc = Descriptor::New<TestDesc>();
         descs.push_back(desc);
      }
      for (auto desc : descs) {
         delete desc;
      }
   }
}

namespace FileViewTests {
   void test_direct_1() {
      size_t fsize = 100000000;
      auto fv = mem::DirectFileView::NewReadWrite("./ee.tmp", fsize, true);
      auto buf = fv->MapBuffer(0, 1000);
      auto bytes = fv->GetBase().as<char>();
      for (size_t s = 16; s <= fsize; s += 4096) {
         bool r = fv->ExtendSize(s);
         for (size_t i = 16; i < s - 4; i += 4096 * (rand() % 32)) {
            ((int*)&bytes[i])[0] = rand();
         }
         _ASSERT(r);
      }
      mem::PrintMemoryInfos();
      delete fv;
   }
}

namespace RegionsTests {
   void test_basic() {
      auto ptr = mem::AllocateUnmanagedRegion(24, 0, 0);

   }
   void test_defrag() {
   }
}

int main() {
   mem::InitializeMemory();

   DescriptorsTests::test_descriptor_region();
   DescriptorsTests::test_descriptor_region();

   RegionsTests::test_basic();
   //RegionsTests::test_defrag();

   //FileViewTests::test_direct_1();

   return 0;
}

