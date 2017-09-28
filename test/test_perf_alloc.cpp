#include "./test_perf_alloc.h"

struct tTest {
  static const int sizeMin = 10;
  static const int sizeMax = 5000;
  static const int count = 100000;

  __declspec(noinline) void test_sat_malloc_2()
  {
    Chrono c;
    std::vector<void*> objects(count);
    int sizeDelta = sizeMax - sizeMin;
    c.Start();
    printf(">>> start: %lg s\n", sat_get_contoller()->getCurrentTime());
    for (int i = 0; i < count; i++) {
      int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
      SAT::tObjectInfos infos;
      objects[i] = sat_malloc_ex(size, 4887);
      // printf(">> %d bytes at %.8X\n", int(size), uintptr_t(objects[i]));
    }
    printf("[SAT-malloc] alloc time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
    printf(">>> end: %lg s\n", sat_get_contoller()->getCurrentTime());
    c.Start();

    int remain = 5;
    SAT::tObjectInfos infos;
    if (0) {
      for (int i = remain; i < count; i++) {
        int k = fastrand() % objects.size();

        void* obj = objects[k];
        objects[k] = objects.back();
        objects.pop_back();
        sat_free(obj);
      }
    }
    else if (1) {
      for (int i = remain; i < count; i++) {
        int k = objects.size() - 1;
        void* obj = objects[k];

        if (sat_get_address_infos(obj, &infos)) {
          if (infos.heapID != 0) {
            printf("bad heapID\n");
          }
        }
        else {
          printf("bad object\n");
        }

        objects.pop_back();
        sat_free(obj);

        /*for (int j = objects.size() - 1; j >= 0; j--) {
        if (sat_get_address_infos(objects[j], &infos)) {
        if (infos.heapID != 0) throw std::exception("bad heapID");
        }
        else {
        throw std::exception("bad object");
        }
        }*/
      }
    }
    else {
      for (int i = remain; i < count; i++) {
        void* obj = objects[i];
        sat_free(obj);
      }
    }
    printf("[SAT-malloc] free time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
    for (int i = 0; i < remain; i++) {
      printf(">> remain at %.8X\n", uintptr_t(objects[i]));
    }

    return;
  }

  __declspec(noinline) void test_sat_malloc() {
    Chrono c;
    std::vector<void*> objects(count);
    int sizeDelta = sizeMax - sizeMin;
    c.Start();
    for (int i = 0; i < count; i++) {
      int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
      objects[i] = sat_malloc(size);
    }
    printf("[SAT-malloc] alloc time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));

    c.Start();
    for (int i = 0; i < count; i++) {
      sat_free(objects[i]);
    }
    printf("[SAT-malloc] free time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
  }

#ifdef USE_TCMALLOC
  __declspec(noinline) void test_tc_malloc() {
    Chrono c;
    std::vector<void*> objects(count);
    int sizeDelta = sizeMax - sizeMin;
    c.Start();
    for (int i = 0; i < count; i++) {
      int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
      objects[i] = tc_malloc(size);
    }
    printf("[TC-malloc] alloc time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));

    c.Start();
    for (int i = 0; i < count; i++) {
      tc_free(objects[i]);
    }
    printf("[TC-malloc] free time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
  }
#endif

  __declspec(noinline) void test_default_malloc() {
    Chrono c;
    std::vector<void*> objects(count);
    int sizeDelta = sizeMax - sizeMin;
    c.Start();
    for (int i = 0; i < count; i++) {
      int size = sizeMin + (sizeDelta ? fastrand() % sizeDelta : 0);
      objects[i] = ::malloc(size);
    }
    printf("[Default-malloc] alloc time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));

    c.Start();
    for (int i = 0; i < count; i++) {
      ::free(objects[i]);
    }
    printf("[Default-malloc] free time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
  }

  __declspec(noinline) void test_alloc_perf()
  {
    int size_min = 10, size_max = 4000;
    MultiThreadAllocTest<4> multi;
#define TestID 1
#if TestID == 1
    DefaultAllocTest().Run(size_min, size_max);
    multi.Run(&DefaultAllocTest(), size_min, size_max);
#elif TestID == 2
    TCmallocAllocTest().Run(size_min, size_max);
    multi.Run(&TCmallocAllocTest(), size_min, size_max);
#elif TestID == 3
    SATmallocAllocTest().Run(size_min, size_max);
    multi.Run(&SATmallocAllocTest(), size_min, size_max);
#endif
  }

  void test_alloc()
  {
    SAT::IController* sat = sat_get_contoller();
    SAT::IThread* thread = sat->getCurrentThread();
    SAT::Mark mark;

    SAT::Mark test_mark = sat_mark_message("test", "Test begin");

    mark = sat_mark_message("test", "sat_malloc test 2");
    this->test_sat_malloc_2();
    mark->complete();
    Sleep(50);

#ifdef USE_TCMALLOC
    mark = sat_mark_message("test", "tc_malloc");
    this->test_tc_malloc();
    mark->complete();
    Sleep(50);
#endif

    mark = sat_mark_message("test", "sat_malloc");
    this->test_sat_malloc();
    mark->complete();
    Sleep(50);

    mark = sat_mark_message("test", "default_malloc");
    this->test_default_malloc();
    sat_marking_print();
    mark->complete();

    test_mark->complete();

  }
};


void test_perf_alloc() {
  tTest test;

#ifdef USE_TCMALLOC
  ::_tcmalloc();
#endif

  //test_types();
  //test.test_alloc_perf();
  test.test_alloc();
}