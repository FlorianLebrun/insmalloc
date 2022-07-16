#include "./handlers.h"

using namespace ins;

extern void test_perf_alloc();
extern void test_buddy_bitmap();
extern void test_unit_span_alloc();
extern void test_unit_fragment_alloc();
extern void test_page_access();

extern void test_objects_allocate();

ins::MemoryContext* ins_malloc_handler::context = 0;


class GCObject {
public:
   void* operator new (size_t sz) {
      return ins_malloc_handler::context->allocateBlock(sz);
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
   ins::GarbageCollector gc(ins_malloc_handler::context->space);
   Test1* obj = new Test1(10);
   gc.roots.push_back(obj);
   gc.scavenge();
   obj->a->a->a = 0;
   gc.scavenge();
   obj->a = 0;
   gc.scavenge();
}

int main() {
   ins::PatchMemoryFunctions();

   ins_malloc_handler::init();

   test_gc();
   //test_buddy_bitmap();
   //test_unit_span_alloc();
   //test_unit_fragment_alloc();
   //test_page_access();
   //test_perf_alloc();
   //test_objects_allocate();

   ins_malloc_handler::context->scavenge();
   ins_malloc_handler::context->space->scavengeCaches();

   ins_malloc_handler::context->getStats();
   ins_malloc_handler::context->space->getStats();

   return 0;
}

