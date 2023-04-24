#include <ins/memory/space.h>
#include <ins/memory/controller.h>

using namespace ins;

namespace ins::mem {
   void __notify_memory_item_init__(uint32_t flag) {
      mem::Controller.Initiate();
   }
}
