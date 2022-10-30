#include <ins/memory/space.h>
#include <ins/memory/controller.h>

using namespace ins;

namespace ins {
   void DescriptorsHeap__init__();
   void __notify_memory_item_init__(uint32_t flag) {
      static uint32_t init_flags = 0;
      init_flags |= flag;
      if (init_flags == 3) {
         ins::RegionsHeap.Initiate();
         ins::DescriptorsHeap__init__();
         ins::Controller.Initiate();
      }
   }
}
