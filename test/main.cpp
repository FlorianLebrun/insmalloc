#include "./handlers.h"

using namespace sat;

extern void test_perf_alloc();
extern void test_buddy_bitmap();
extern void test_unit_span_alloc();
extern void test_unit_fragment_alloc();
extern void test_page_access();

extern void test_objects_allocate();

sat::MemoryContext* sat_malloc_handler::context = 0;


class GCObject {
public:
   void* operator new (size_t sz) {
      return sat_malloc_handler::context->allocateBlock(sz);
   }
};

class Test1 : public GCObject {
public:
   Test1* a = 0;
   Test1(int l) {
      if (l) a = new Test1(l - 1);
   }
};

void test_gc() {
   sat::GarbageCollector gc(sat_malloc_handler::context->space);
   Test1* obj = new Test1(10);
   gc.roots.push_back(obj);
   gc.scavenge();
   obj->a->a->a = 0;
   gc.scavenge();
   obj->a = 0;
   gc.scavenge();
}

int main() {
   sat_malloc_handler::init();

   test_gc();
   //test_buddy_bitmap();
   //test_unit_span_alloc();
   //test_unit_fragment_alloc();
   //test_page_access();
   //test_perf_alloc();
   //test_objects_allocate();

   sat_malloc_handler::context->scavenge();
   sat_malloc_handler::context->space->scavengeCaches();

   sat_malloc_handler::context->getStats();
   sat_malloc_handler::context->space->getStats();

   return 0;
}

