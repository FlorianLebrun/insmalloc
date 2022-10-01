#include "./test_perf_alloc.h"
#include <vector>
#include <mimalloc.h>
#include <intrin.h>

#define USE_MIMALLOC 1

struct tTest {
   int sizeMin = 0;
   int sizeMax = 0;
   int count = 0;

   int test_remain_count = 0;

   void setSizeProfile(int sizeMin, int sizeMax = 0, int count = 0) {
      const int cMaxMemoryUse = 3000000000;
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

   /*__declspec(noinline) void test_ins_malloc_2()
   {
      Chrono c;
      std::vector<void*> objects(count);
      int sizeDelta = sizeMax - sizeMin;
      printf(">>> start: %lg s\n", ins::timing::getCurrentTime());
      c.Start();
      for (int i = 0; i < count; i++) {
         int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
         ins::tObjectInfos infos;
         auto obj = ins_malloc_ex(size, 4887);
         objects[i] = obj;
         //memset(objects[i], 0, size);
         if (!ins_get_address_infos(obj, &infos))
            printf("bad object\n");
         else if (infos.heapID != 0) printf("bad heapID\n");
         // printf(">> %d bytes at %.8X\n", int(size), uintptr_t(objects[i]));
      }
      printf("[ins-malloc] alloc time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
      printf(">>> end: %lg s\n", ins::timing::getCurrentTime());
      c.Start();

      ins::tObjectInfos infos;
      if (0) {
         for (int i = test_remain_count; i < count; i++) {
            int k = fastrand() % objects.size();
            void* obj = objects[k];
            objects[k] = objects.back();
            objects.pop_back();
            ins_free(obj);
         }
      }
      else if (1) {
         for (int i = test_remain_count; i < count; i++) {
            int k = objects.size() - 1;
            void* obj = objects[k];
            if (!ins_get_address_infos(obj, &infos)) printf("bad object\n");
            else if (infos.heapID != 0) printf("bad heapID\n");
            objects.pop_back();
            ins_free(obj);
         }
      }
      else {
         for (int i = test_remain_count; i < count; i++) {
            void* obj = objects[i];
            ins_free(obj);
         }
      }
      printf("[ins-malloc] free time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
      ins_flush_cache();
      ins_flush_cache();
      ins_flush_cache();
      ins_flush_cache();
      ins_flush_cache();
      ins::memory::checkObjectsOverflow();
      //ins::memory::table.print();
      //system("pause");
      for (int i = 0; i < test_remain_count; i++) {
         printf(">> remain at %.12llX\n", int64_t(objects[i]));
      }

      return;
   }*/

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
      }
      auto alloc_time_ns = c.GetDiffFloat(Chrono::NS);

      c.Start();
      for (int i = 0; i < count; i++) {
         handler::free(objects[i]);
      }
      auto free_time_ns = c.GetDiffFloat(Chrono::NS);

      printf("[%s] time = %g ns (alloc = %g, free = %g)\n", handler::name(), (alloc_time_ns + free_time_ns) / float(count), alloc_time_ns / float(count), free_time_ns / float(count));

      wait_ms(50);
   }

   template<class handler>
   __declspec(noinline) void apply_peak_drop(int alloc_count, int free_count) {
      Chrono c;
      std::vector<void*> objects(count);
      int sizeDelta = sizeMax - sizeMin;
      intptr_t ops = 0;

      c.Start();
      intptr_t i = 0;
      intptr_t count = this->count/2 - alloc_count;
      while (i < count) {
         for (int k = 0; k < alloc_count; k++) {
            int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
            auto ptr = handler::malloc(size);
            //for (intptr_t k = 0; k < i; k++) INS_ASSERT(objects[k] != ptr);
            objects[i++] = ptr;
            ops++;
         }
         for (int k = 0; k < free_count; k++) {
            int p = fastrand() % i;
            handler::free(objects[p]);
            objects[p] = objects[--i];
            objects[i] = 0;
            ops++;
         }
      }

      for (int p = 0; p < i; p++) {
         handler::free(objects[p]);
         ops++;
      }

      printf("[%s] time = %g ns\n", handler::name(), c.GetDiffFloat(Chrono::NS) / float(ops));

      wait_ms(50);
   }

   __declspec(noinline) void test_multi_thread_perf()
   {
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
         //this->apply_fill_and_flush<no_malloc_handler>();
         //this->apply_fill_and_flush<default_malloc_handler>();
         this->apply_fill_and_flush<ins_malloc_handler>();
         //this->apply_fill_and_flush<mi_malloc_handler>();
         printf("                     * * *\n");
      }
   }
   void test_peak_drop(int alloc_count, int free_count) {
      if (alloc_count <= free_count) throw;
      printf("---------------- Pattern: peak - drop --------------------\n");
      for (int i = 0; i < 3; i++) {
         //this->apply_peak_drop<no_malloc_handler>(alloc_count, free_count);
         //this->apply_peak_drop<default_malloc_handler>(alloc_count, free_count);
         this->apply_peak_drop<ins_malloc_handler>(alloc_count, free_count);
         //this->apply_peak_drop<mi_malloc_handler>(alloc_count, free_count);
         printf("                     * * *\n");
      }
   }
};


void test_perf_alloc() {

   tTest test;
   test.setSizeProfile(16, 12560);
   //test.setSmallSizeProfile();
   //test.setMediumSizeProfile();
   //test.setBigSizeProfile();
   printf("------------------ Test perf alloc ------------------\n");

   //test.test_ins_malloc_2();
   //test.test_alloc_perf();
   test.test_peak_drop(10, 8);
   //test.test_fill_and_flush();
   //test.test_multi_thread_perf();
   printf("------------------ end ------------------\n");
}
