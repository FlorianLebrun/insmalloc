#include "./index.h"

using namespace sat::Slabbing;
int SlabbingSegment::free(uintptr_t index, uintptr_t ptr) {
   allocator->free((void*)ptr);
   return 0;
}
