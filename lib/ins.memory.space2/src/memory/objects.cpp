#include <stdint.h>
#include <semaphore>
#include <stdlib.h>
#include "./objects.h"
#include "../os/memory.h"

using namespace ins;

void test_small_object() {

   uintptr_t region_buf = OSMemory::AllocBuffer(0, 0, cst::RegionSize, cst::RegionSize);
   ObjectRegion region = new (malloc(cst::RegionSize)) sObjectRegion(size_target_t(64));

   ObjectSlab slab = region->AcquireSlab();
   ObjectHeader obj = region->AllocateObject();

}
