#include <sat/memory/allocator.hpp>
#include "./utils.h"
#include <vector>

struct tTestSegment : sat::MemorySegmentController {
   virtual const char* getName() {
      return "TEST-SEG";
   }
} TestSegment;

uintptr_t allocSegments(size_t size) {
   auto base = sat::memory::allocSegmentSpan(size);
   for (size_t i = 0; i < size; i++) sat::memory::table[i + base] = &TestSegment;
   return base;
}

struct test_perf_segment_alloc_s {
   struct tMemSpan {
      uintptr_t base;
      size_t size;
   };
   static const int count = 2000;
   std::vector<tMemSpan> mems;

   test_perf_segment_alloc_s()
      : mems(count) {
   }
   void run_alloc_free() {
      Chrono c;
      int total = 0;

      c.Start();
      for (int i = 0; i < count; i++) {
         auto& m = mems[i];
         m.size = size_t(1) + size_t(rand()) % 100;
         m.base = allocSegments(m.size);
         memset((void*)(m.base << sat::memory::cSegmentSizeL2), 0, m.size << sat::memory::cSegmentSizeL2);
         total += m.size;
      }
      printf("[alloc] time = %g us\n", c.GetDiffFloat(Chrono::US) / float(count));
      sat::memory::table.print();

      c.Start();
      for (int i = 0; i < count; i++) {
         auto& m = mems[i];
         sat::memory::freeSegmentSpan(m.base, m.size);
         m.base = 0;
         m.size = 0;
      }
      printf("[free] time = %g us\n", c.GetDiffFloat(Chrono::US) / float(count));
      sat::memory::table.print();

      printf("------------------------------------\n");
   }
   void run() {
      printf("------------------ Test perf segments ------------------\n");
      run_alloc_free();
      wait_ms(50);
      run_alloc_free();
   }
};

void test_segment_alloc() {
   test_perf_segment_alloc_s().run();
}
