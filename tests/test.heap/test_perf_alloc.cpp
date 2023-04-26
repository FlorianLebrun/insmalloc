#include "./test_perf_alloc.h"
#include <vector>
#include <mimalloc.h>
#include <intrin.h>

#define USE_MIMALLOC 1

using namespace ins;

struct tTest {
   int sizeMin = 0;
   int sizeMax = 0;
   int count = 0;

   int test_remain_count = 0;

   void setSizeProfile(int sizeMin, int sizeMax = 0, int count = 0) {
      const size_t cMaxMemoryUse = 1800000000;
      if (!sizeMax) sizeMax = sizeMin;
      if (!count) count = cMaxMemoryUse * 2 / (sizeMax + sizeMin);
      this->sizeMin = sizeMin;
      this->sizeMax = sizeMax;
      this->count = count;
   }
   void setSmallSizeProfile() {
      this->sizeMin = 10;
      this->sizeMax = 500;
      this->count = 14000000;
   }
   void setMediumSizeProfile() {
      this->sizeMin = 100;
      this->sizeMax = 5000;
      this->count = 1400000;
   }
   void setBigSizeProfile() {
      this->sizeMin = 10;
      this->sizeMax = 50000;
      this->count = 140000;
   }

   template<class handler>
   __declspec(noinline) void apply_fill_and_flush() {
      Chrono c;
      std::vector<void*> objects(count);
      int sizeDelta = sizeMax - sizeMin;

      c.Start();
      for (int i = 0; i < count; i++) {
         int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
         auto ptr = objects[i] = handler::malloc(size);
         if (!ptr) throw;
         mem::ObjectBytes((void*)ptr)[0] = 1;
      }
      auto alloc_time_ns = c.GetDiffFloat(Chrono::NS);

      c.Start();
      for (int i = 0; i < count; i++) {
         auto ptr = objects[i];
         mem::ObjectBytes((void*)ptr)[0] = 1;
         handler::free(ptr);
      }
      auto free_time_ns = c.GetDiffFloat(Chrono::NS);

      printf("[%s] time = %g ns (alloc = %g, free = %g)\n", handler::name(), (alloc_time_ns + free_time_ns) / float(count), alloc_time_ns / float(count), free_time_ns / float(count));

      wait_ms(50);
   }

   template<class handler>
   __declspec(noinline) void apply_peak_drop(int alloc_count, int free_count, intptr_t max_cycle) {
      Chrono c;
      std::vector<void*> objects(count);
      int sizeDelta = sizeMax - sizeMin;
      intptr_t ops = 0;

      c.Start();
      intptr_t i = 0;
      intptr_t count = this->count / 2 - alloc_count;
      intptr_t cycle = 0;
      while (i < count && cycle < max_cycle) {
         for (int k = 0; k < alloc_count; k++) {
            int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
            auto ptr = handler::malloc(size);
            _ASSERT(uintptr_t(ptr) > 1000);
            mem::ObjectBytes((void*)ptr)[0] = 1;
            //for (intptr_t k = 0; k < i; k++) INS_ASSERT(objects[k] != ptr);
            objects[i++] = ptr;
            ops++;
         }
         for (int k = 0; k < free_count; k++) {
            int p = fastrand() % i;
            auto ptr = objects[p];
            mem::ObjectBytes((void*)ptr)[0] = 1;
            handler::free(ptr);
            objects[p] = objects[--i];
            objects[i] = 0;
            ops++;
         }
         cycle++;
      }

      for (int p = 0; p < i; p++) {
         auto ptr = objects[p];
         mem::ObjectBytes((void*)ptr)[0] = 1;
         handler::free(objects[p]);
         ops++;
      }

      printf("[%s] time = %g ns\n", handler::name(), c.GetDiffFloat(Chrono::NS) / float(ops));

      wait_ms(50);
   }

   __declspec(noinline) void test_multi_thread_perf() {
      int size_min = 10, size_max = 4000;
      MultiThreadAllocTest<4> multi;

#define TestID 2
#if TestID == 1
      GenericAllocTest<default_malloc_handler>().Run(size_min, size_max);
      multi.Run(&GenericAllocTest<default_malloc_handler>(), size_min, size_max);
#elif TestID == 2
      GenericAllocTest<mi_malloc_handler>().Run(size_min, size_max);
      multi.Run(&GenericAllocTest<mi_malloc_handler>(), size_min, size_max);
#elif TestID == 3
      GenericAllocTest<ins_malloc_handler>().Run(size_min, size_max);
      multi.Run(&GenericAllocTest<ins_malloc_handler>(), size_min, size_max);
#endif
   }

   void test_fill_and_flush() {
      printf("---------------- Pattern: fill & flush --------------------\n");
      for (int i = 0; i < 3; i++) {
#ifndef _DEBUG
         //this->apply_fill_and_flush<no_malloc_handler>();
         //this->apply_fill_and_flush<default_malloc_handler>();
         this->apply_fill_and_flush<mi_malloc_handler>();
#endif
         this->apply_fill_and_flush<ins_malloc_handler>();
         printf("                     * * *\n");
      }
   }

   void test_peak_drop(int alloc_count, int free_count, intptr_t max_cycle) {
      if (alloc_count < free_count) throw;
      printf("---------------- Pattern: peak - drop --------------------\n");
      for (int i = 0; i < 3; i++) {
#ifndef _DEBUG
         //this->apply_peak_drop<no_malloc_handler>(alloc_count, free_count, max_cycle);
         //this->apply_peak_drop<default_malloc_handler>(alloc_count, free_count, max_cycle);
         this->apply_peak_drop<mi_malloc_handler>(alloc_count, free_count, max_cycle);
#endif
         this->apply_peak_drop<ins_malloc_handler>(alloc_count, free_count, max_cycle);
         printf("                     * * *\n");
      }
   }
};


void test_perf_alloc() {
#if _DEBUG
   size_t max_cycle = 10000;
   size_t max_count = 100000;
#else
   size_t max_cycle = 5000000;
   size_t max_count = 0;
#endif

   tTest test;
   test.setSizeProfile(20, 1020, max_count);
   //test.setSmallSizeProfile();
   //test.setMediumSizeProfile();
   //test.setBigSizeProfile();
   printf("------------------ Test perf alloc ------------------\n");

   //test.test_peak_drop(20, 20, max_cycle);
   test.test_peak_drop(100, 50, max_cycle);

   test.test_fill_and_flush();

   printf("------------------ end ------------------\n");
}
