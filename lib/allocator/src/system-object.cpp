#include "./allocators/Slabbing/index.h"
#include <sat/memory/system-object.hpp>

static sat::Slabbing::GlobalSlabbingAllocator system_allocator;

void* sat::system_object::allocSystemBuffer(size_t size) {
   return system_allocator.allocate(size);
}
