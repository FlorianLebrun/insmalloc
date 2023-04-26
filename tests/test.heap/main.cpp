#include <ins/memory/file-view.h>
#include <ins/memory/schemas.h>
#include <ins/memory/contexts.h>
#include <ins/memory/controller.h>
#include <ins/timing.h>
#include <stdio.h>
#include <vector>
#include <unordered_map>

#include "./threading.h"
#include "./test_perf_alloc.h"

using namespace ins;

namespace ManagedObjectsTests {

   struct MyClass : mem::ManagedClass<MyClass> {
      Ref<MyClass> parent;
      Ref<MyClass> next;
      //std::string name = "hello";
      ~MyClass() {
         printf("> finalize MyClass %p\n", this);
      }
      static void __traverser__(mem::TraversalContext<mem::sObjectSchema, MyClass>& context) {
         context.visit_ref(offsetof(MyClass, parent));
         context.visit_ref(offsetof(MyClass, next));
         context.visit_ref(offsetof(MyClass, parent));
         context.visit_ref(offsetof(MyClass, next));
         context.visit_ref(offsetof(MyClass, parent));
         context.visit_ref(offsetof(MyClass, next));
      }
   };
   void apply_alloc_gc() {
      mem::ThreadMemoryContext context;
      mem::ThreadStackTracker tracker;
      mem::Local<MyClass> x = new MyClass();
      mem::PerformHeapCleanup();

      x->next = new MyClass();
      x->next = new MyClass();
      x->next = new MyClass();
      x->next = new MyClass();
      x->next = new MyClass();
      x->next->next = x;

      if (1) {
         ins::timing::Chrono chrono;
         MyClass* cur = x.get();
         for (int i = 0; i < 100000000; i++) {
            MyClass* c = new MyClass();
            c->parent = cur;
            cur->next = c;
            cur = c;
         }
         printf("> allocation time: %g s\n", chrono.GetDiffFloat(chrono.S));
      }

      mem::PerformHeapCleanup();

      x.release();

      mem::PerformHeapCleanup();

      //MemorySpace::Print();
   }
}

mem::ManagedSchema mem::ManagedClass<ManagedObjectsTests::MyClass>::schema;

int main() {
   mem::InitializeHeap();

   //DescriptorsTests::test_descriptor_region();
   if (0) {
      void* p;
      p = mem::AllocateObject(6400000);
      mem::FreeObject(p);
      p = mem::AllocateObject(64000);
      mem::FreeObject(p);
   }
   if (1) {
      auto p = new ManagedObjectsTests::MyClass();
      _ASSERT(mem::ObjectLocation(p).IsAlive() == true);
      mem::RetainObjectWeak(p);
      mem::ReleaseObject(p);
      _ASSERT(mem::ObjectLocation(p).IsAlive() == false);
      mem::ReleaseObjectWeak(p);
   }
   if (0) {
      mem::SetTimeStampOption(true);
      mem::SetStackStampOption(true);
      mem::SetSecurityPaddingOption(60);
   }
   if (0) {
      ManagedObjectsTests::apply_alloc_gc();
      return 0;
   }
   if (0) {
      auto a = mem::AllocateObject(50);
      auto b = mem::AllocateObject(50);
      auto p = mem::AllocateObject(50);
      mem::ObjectInfos infos(p);
      auto p_sz = infos.GetUsableSize();
      if (mem::ObjectInfos(p).GetAnalyticsInfos()) {
         printf("");
      }
      mem::FreeObject(p);
      mem::PrintMemoryInfos();

      mem::FreeObject(a);
      mem::FreeObject(b);
   }
   if (1) {
      printf("------------ Monothread --------------\n");
      mem::ThreadMemoryContext context;
      test_perf_alloc();
   }
   if (0) {
      printf("------------ Cross-context --------------\n");
      mem::SetMaxUsablePhysicalBytes(size_t(1) << 31);
      size_t limit = mem::GetMaxUsablePhysicalBytes() * 0.50;
      size_t szblk = mem::cst::ObjectLayoutBase[4].object_multiplier;
      size_t count = limit / szblk;

      void** p = new void* [count];
      {
         mem::ThreadMemoryContext context;
         for (size_t i = 0; i < count; i++) {
            p[i] = mem::AllocateObject(szblk);
         }
         context.Pop();
         mem::PrintInfos();
      }
      {
         mem::ThreadMemoryContext context;
         for (size_t i = 0; i < count; i++) {
            mem::FreeObject(p[i]);
            p[i] = mem::AllocateObject(szblk);
         }
         mem::PrintInfos();
         for (size_t i = 0; i < count; i++) {
            mem::FreeObject(p[i]);
         }
         delete p;
         mem::PrintInfos();
      }
   }
   if (0) {
      printf("------------ Multithread --------------\n");
      std::thread t1(
         []() {
            mem::ThreadMemoryContext context;
            test_perf_alloc();
         }
      );
      std::thread t2(
         []() {
            mem::ThreadMemoryContext context;
            test_perf_alloc();
         }
      );
      std::thread t3(
         []() {
            mem::ThreadMemoryContext context;
            test_perf_alloc();
         }
      );
      std::thread t4(
         []() {
            mem::ThreadMemoryContext context;
            test_perf_alloc();
         }
      );
      t1.join();
      t2.join();
      t3.join();
      t4.join();
   }
   if (0) {
      printf("------------ Sequential overflow --------------\n");
      mem::SetMaxUsablePhysicalBytes(size_t(1) << 30);
      size_t limit = mem::GetMaxUsablePhysicalBytes() * 0.8;
      size_t szblk = mem::cst::ObjectLayoutBase[4].object_multiplier;
      size_t count = limit / szblk;
      std::thread t1(
         [&]() {
            mem::ThreadMemoryContext context;
            void** p = new void* [count];
            for (size_t i = 0; i < count; i++) {
               p[i] = mem::AllocateObject(szblk);
            }
            for (size_t i = 0; i < count; i++) {
               mem::FreeObject(p[i]);
            }
            delete p;
            context.Pop();
         }
      );
      t1.join();
      std::thread t2(
         [&]() {
            mem::ThreadMemoryContext context;
            for (size_t sz = 0; sz < limit; sz += szblk) {
               mem::AllocateObject(szblk);
            }
         }
      );
      t2.join();
      mem::PrintInfos();
   }
   {
      mem::PerformHeapCleanup();
      mem::PrintInfos();
   }

   return 0;
}

