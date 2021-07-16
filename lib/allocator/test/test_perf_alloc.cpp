#include "./test_perf_alloc.h"
#include <vector>
#include <mimalloc.h>
#include <intrin.h>

#define USE_MIMALLOC 1

extern"C" void* mpc_malloc(size_t size);
extern"C" void mpc_free(void* p);

struct no_malloc_handler {
   static const char* name() {
      return "no-malloc";
   }
   static void* malloc(size_t) {
      __nop();
      return 0;
   }
   static void free(void*) {
      __nop();
   }
};

struct mi_malloc_handler {
   static const char* name() {
      return "mi-malloc";
   }
   static void* malloc(size_t s) {
      return mi_malloc(s);
   }
   static void free(void* p) {
      return mi_free(p);
   }
};

struct mpc_malloc_handler {
   static const char* name() {
      return "mpc-malloc";
   }
   static void* malloc(size_t s) {
      return mpc_malloc(s);
   }
   static void free(void* p) {
      return sat_free(p);
   }
};

struct sat_malloc_handler {
   static const char* name() {
      return "sat-malloc";
   }
   static void* malloc(size_t s) {
      return sat_malloc(s);
   }
   static void free(void* p) {
      return sat_free(p);
   }
};

struct default_malloc_handler {
   static const char* name() {
      return "default-malloc";
   }
   static void* malloc(size_t s) {
      return malloc(s);
   }
   static void free(void* p) {
      return free(p);
   }
};

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

   __declspec(noinline) void test_sat_malloc_2()
   {
      Chrono c;
      std::vector<void*> objects(count);
      int sizeDelta = sizeMax - sizeMin;
      printf(">>> start: %lg s\n", sat::timing::getCurrentTime());
      c.Start();
      for (int i = 0; i < count; i++) {
         int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
         sat::tObjectInfos infos;
         auto obj = sat_malloc_ex(size, 4887);
         objects[i] = obj;
         //memset(objects[i], 0, size);
         if (!sat_get_address_infos(obj, &infos))
            printf("bad object\n");
         else if (infos.heapID != 0) printf("bad heapID\n");
         // printf(">> %d bytes at %.8X\n", int(size), uintptr_t(objects[i]));
      }
      printf("[sat-malloc] alloc time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
      printf(">>> end: %lg s\n", sat::timing::getCurrentTime());
      c.Start();

      sat::tObjectInfos infos;
      if (0) {
         for (int i = test_remain_count; i < count; i++) {
            int k = fastrand() % objects.size();
            void* obj = objects[k];
            objects[k] = objects.back();
            objects.pop_back();
            sat_free(obj);
         }
      }
      else if (1) {
         for (int i = test_remain_count; i < count; i++) {
            int k = objects.size() - 1;
            void* obj = objects[k];
            if (!sat_get_address_infos(obj, &infos)) printf("bad object\n");
            else if (infos.heapID != 0) printf("bad heapID\n");
            objects.pop_back();
            sat_free(obj);
         }
      }
      else {
         for (int i = test_remain_count; i < count; i++) {
            void* obj = objects[i];
            sat_free(obj);
         }
      }
      printf("[sat-malloc] free time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
      sat_flush_cache();
      sat_flush_cache();
      sat_flush_cache();
      sat_flush_cache();
      sat_flush_cache();
      sat::memory::checkObjectsOverflow();
      //sat::memory::table.print();
      //system("pause");
      for (int i = 0; i < test_remain_count; i++) {
         printf(">> remain at %.12llX\n", int64_t(objects[i]));
      }

      return;
   }

   template<class handler>
   __declspec(noinline) void apply_fill_and_flush() {
      Chrono c;
      std::vector<void*> objects(count);
      int sizeDelta = sizeMax - sizeMin;

      c.Start();
      for (int i = 0; i < count; i++) {
         int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
         objects[i] = handler::malloc(size);
      }
      printf("[%s] alloc time = %g ns\n", handler::name(), c.GetDiffFloat(Chrono::NS) / float(count));

      c.Start();
      for (int i = 0; i < count; i++) {
         handler::free(objects[i]);
      }
      printf("[%s] free time = %g ns\n", handler::name(), c.GetDiffFloat(Chrono::NS) / float(count));

      wait_ms(50);
   }

   __declspec(noinline) void test_multi_thread_perf()
   {
      int size_min = 10, size_max = 4000;
      MultiThreadAllocTest<4> multi;

#define TestID 1
#if TestID == 1
      GenericAllocTest<default_malloc_handler>().Run(size_min, size_max);
      multi.Run(&GenericAllocTest<default_malloc_handler>(), size_min, size_max);
#elif TestID == 2
      GenericAllocTest<mi_malloc_handler>().Run(size_min, size_max);
      multi.Run(&GenericAllocTest<mi_malloc_handler>(), size_min, size_max);
#elif TestID == 3
      GenericAllocTest<sat_malloc_handler>().Run(size_min, size_max);
      multi.Run(&GenericAllocTest<sat_malloc_handler>(), size_min, size_max);
#endif
   }

   void test_fill_and_flush() {
      for (int i = 0; i < 3; i++) {
         this->apply_fill_and_flush<no_malloc_handler>();
         this->apply_fill_and_flush<mi_malloc_handler>();
         this->apply_fill_and_flush<mpc_malloc_handler>();
         //this->apply_fill_and_flush<default_malloc_handler>();
         this->apply_fill_and_flush<sat_malloc_handler>();
         printf("------------------------------------\n");
      }
   }
};


void test_perf_alloc() {
   printf("------------------ Test perf alloc ------------------\n");
   tTest test;
   test.setSizeProfile(100);
   //test.setSmallSizeProfile();
   //test.setMediumSizeProfile();
   //test.setBigSizeProfile();

#ifdef USE_TCMALLOC
   ::_tcmalloc();
#endif

   //test.test_sat_malloc_2();
   //test.test_alloc_perf();
   test.test_fill_and_flush();
}
