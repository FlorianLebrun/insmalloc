#include <sat/memory/allocator.hpp>
#include <stdio.h>

extern void test_segment_alloc();
extern void test_meta_alloc();
extern void test_perf_alloc();
extern void test_buffer_alloc();
extern void test_btree();
extern void test_stack_analysis();

int main() {
   //sat_patch_default_allocator();

   //test_buffer_alloc();
   //test_meta_alloc();
   //test_perf_alloc();
   //test_btree();
   test_segment_alloc();

   fflush(stdout);
   getchar();
   return 0;
}
