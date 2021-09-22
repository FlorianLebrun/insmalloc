#include "./handlers.h"
#include <stdio.h>
#include <functional>
#include <atomic>
#include <vector>

using namespace sat;

extern"C" sat::BlockLocation s_block;
sat::BlockLocation s_block;

typedef sat_malloc_handler TestAllocator;
//typedef mi_malloc_handler TestAllocator;
//typedef default_malloc_handler TestAllocator;

void test_block_location_perf(MemoryContext& context) {
   printf("\n-------------- Test performance --------------\n");
   int count = 200000;
   Chrono c;

   address_t address = context.allocateBlock(40);

   c.Start();
   for (int i = 0; i < count; i++) {
      s_block.set(context.space, address);
   }
   printf("[block location] time = %lg ns\n", c.GetDiffDouble(Chrono::NS) / double(count));

}

void test_allocation_perf(MemoryContext& context, size_t size) {
   printf("\n-------------- Test performance --------------\n");
   int count = 3500000000 / size;
   Chrono c;

   std::vector<void*> blocks(count);

   c.Start();
   for (int i = 0; i < count; i++) {
      blocks[i] = TestAllocator::malloc(size);
   }
   printf("[block alloc] time = %lg ns\n", c.GetDiffDouble(Chrono::NS) / double(count));

   c.Start();
   int numValid = 0;
   for (int i = 0; i < count; i++) {
      if (TestAllocator::check(blocks[i])) numValid++;
      else throw;
   }
   printf("[block check] time = %lg ns (%d)\n", c.GetDiffDouble(Chrono::NS) / double(numValid), numValid);

   c.Start();
   for (int i = 0; i < count; i++) {
      TestAllocator::free(blocks[i]);
   }
   printf("[block free] time = %lg ns\n", c.GetDiffDouble(Chrono::NS) / double(count));

}


void test_objects_allocate() {
   sat_malloc_handler::init();
   MemoryContext& context = *sat_malloc_handler::context;

   sat::printAllBlocks();

   if (1) {
      address_t ptr;
      ptr = context.allocateBlock(4086);
      printf("allocate at 0x%.12llX\n", ptr);
      ptr = context.allocateBlock(76);
      printf("allocate at 0x%.12llX\n", ptr);
      ptr = context.allocateBlock(76000);
      printf("allocate at 0x%.12llX\n", ptr);
      ptr = context.allocateBlock(7600);
      printf("allocate at 0x%.12llX\n", ptr);
      ptr = context.allocateBlock(760);
      printf("allocate at 0x%.12llX\n", ptr);

      ptr = context.allocateBlock(2048);
      ptr = context.allocateBlock(1024);
      ptr = context.allocateBlock(16);

      for (int i = 0; i < 300; i++) {
         ptr = context.allocateBlock(979);
         printf("allocate at 0x%.12llX\n", ptr);
         context.disposeBlock(ptr);
      }
   }

   if (0) {
      test_block_location_perf(context);
   }

   if (1) {
      test_allocation_perf(context, 120);
   }
}
