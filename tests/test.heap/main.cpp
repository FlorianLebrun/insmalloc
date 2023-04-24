#include <ins/memory/space.h>
#include <ins/memory/file-view.h>
#include <ins/memory/schemas.h>
#include <ins/memory/contexts.h>
#include <ins/memory/objects-refs.h>
#include <ins/memory/controller.h>
#include <ins/timing.h>
#include <stdio.h>
#include <vector>
#include <unordered_map>

#include "./test_perf_alloc.h"

using namespace ins;

namespace ObjectsTests {
   /*void apply_cross_context_private_alloc(LocalObjectClassPool& contextAlloc, LocalObjectClassPool& contextDispose) {
      {
         ObjectLocation obj = contextAlloc.AllocatePrivateObject();
         contextDispose.FreeObject(obj);
         contextAlloc.CheckValidity();
      }
      {
         size_t count = 2000;
         std::vector<ObjectHeader> objects;
         for (int i = 0; i < count; i++) {
            objects.push_back(0);
         }
         for (int cycle = 0; cycle < 2; cycle++) {
            for (int i = 0; i < count; i++) {
               //printf("! acquire %d\n", i);
               auto obj = contextAlloc.AllocatePrivateObject();
               objects[i] = obj;
            }
            _INS_DEBUG(MemorySpace::Print());
            contextAlloc.CheckValidity();
            for (int i = 0; i < count; i++) {
               //printf("! dispose %d\n", i);
               ObjectLocation obj = objects[i];
               contextDispose.FreeObject(obj);
            }
            _INS_DEBUG(MemorySpace::Print());
            contextAlloc.CheckValidity();
         }
         contextAlloc.CheckValidity();
      }
   }


   void apply_cross_context_shared_alloc(LocalObjectClassPool& contextAlloc, LocalObjectClassPool& contextDispose) {
      {
         ObjectLocation obj = contextAlloc.AllocateSharedObject();
         contextDispose.FreeObject(obj);
         contextAlloc.CheckValidity();
      }
      {
         size_t count = 2000;
         std::vector<ObjectHeader> objects;
         for (int i = 0; i < count; i++) {
            objects.push_back(0);
         }
         for (int cycle = 0; cycle < 2; cycle++) {
            for (int i = 0; i < count; i++) {
               //printf("! acquire %d\n", i);
               auto obj = contextAlloc.AllocateSharedObject();
               objects[i] = obj;
            }
            _INS_DEBUG(MemorySpace::Print());
            contextAlloc.CheckValidity();
            for (int i = 0; i < count; i++) {
               //printf("! dispose %d\n", i);
               ObjectLocation obj = objects[i];
               contextDispose.FreeObject(obj);
            }
            _INS_DEBUG(MemorySpace::Print());
            contextAlloc.CheckValidity();
         }
         contextAlloc.CheckValidity();
      }
      {
         size_t count = 2000000;
         for (int i = 0; i < count; i++) {
            ObjectLocation obj = contextAlloc.AllocateSharedObject();
            contextDispose.FreeObject(obj);
         }
         _INS_DEBUG(MemorySpace::Print());
         contextAlloc.CheckValidity();
      }
   }

   void test_private_object(MemoryCentralContext& heap) {
      mem::ThreadDedicatedContext contextA(heap.AcquireContext(), true);
      mem::ThreadDedicatedContext contextB(heap.AcquireContext(), true);
      int classId = 4;

      CentralObjectClassPool& heapObj = heap.objects_unmanaged[classId];
      LocalObjectClassPool& contextObjA = contextA->objects_unmanaged[classId];
      LocalObjectClassPool& contextObjB = contextB->objects_unmanaged[classId];

      apply_cross_context_private_alloc(contextObjA, contextObjA);
      apply_cross_context_private_alloc(contextObjA, contextObjB);

   }

   void test_shared_object(MemoryCentralContext& heap) {
      mem::ThreadDedicatedContext contextA(heap.AcquireContext(), true);
      mem::ThreadDedicatedContext contextB(heap.AcquireContext(), true);
      int classId = 4;

      CentralObjectClassPool& heapObj = heap.objects_unmanaged[classId];
      LocalObjectClassPool& contextObjA = contextA->objects_unmanaged[classId];
      LocalObjectClassPool& contextObjB = contextB->objects_unmanaged[classId];

      apply_cross_context_shared_alloc(contextObjA, contextObjA);
      apply_cross_context_shared_alloc(contextObjA, contextObjB);

   }*/

}

namespace ManagedObjectsTests {

   struct MyClass : mem::ManagedClass<MyClass> {
      CRef<MyClass> parent;
      CRef<MyClass> next;
      //std::string name = "hello";
      static void __traverser__(mem::TraversalContext<mem::SchemaDesc, MyClass>& context) {
         context.visit_ref(offsetof(MyClass, parent));
         context.visit_ref(offsetof(MyClass, next));
         context.visit_ref(offsetof(MyClass, parent));
         context.visit_ref(offsetof(MyClass, next));
         context.visit_ref(offsetof(MyClass, parent));
         context.visit_ref(offsetof(MyClass, next));
      }
   };
   void apply_alloc_gc() {
      mem::ThreadDedicatedContext context;
      CLocal<MyClass> x = new MyClass();
      mem::Controller.central.PerformMemoryCleanup();

      x->next = new MyClass();
      x->next = new MyClass();
      x->next = new MyClass();
      x->next = new MyClass();
      x->next = new MyClass();
      x->next->next = x;

      if (1) {
         ins::timing::Chrono chrono;
         MyClass* cur = x.get();
         for (int i = 0; i < 10000000; i++) {
            MyClass* c = new MyClass();
            c->parent = cur;
            cur->next = c;
            cur = c;
         }
         printf("> allocation time: %g s\n", chrono.GetDiffFloat(chrono.S));
      }

      mem::Controller.central.PerformMemoryCleanup();

      x.release();

      mem::Controller.central.PerformMemoryCleanup();

      //MemorySpace::Print();
   }
}

mem::ManagedSchema mem::ManagedClass<ManagedObjectsTests::MyClass>::schema;

int main() {
   //DescriptorsTests::test_descriptor_region();
   if (0) {
      void* p;
      p = ins_malloc(6400000);
      ins_free(p);
      p = ins_malloc(64000);
      ins_free(p);
   }
   if (0) {
      mem::Controller.SetTimeStampOption(true);
      mem::Controller.SetStackStampOption(true);
      mem::Controller.SetSecurityPaddingOption(60);
   }
   if (0) {
      ManagedObjectsTests::apply_alloc_gc();
      return 0;
   }
   if (0) {
      mem::ObjectAnalyticsInfos meta;
      auto a = ins_malloc(50);
      auto b = ins_malloc(50);
      auto p = ins_malloc(50);
      auto p_sz = ins_msize(p);
      if (ins_get_metadata(p, meta)) {
         printf("");
      }
      ins_free(p);
      mem::Regions.Print();

      ins_free(a);
      ins_free(b);
   }
   if (0) {
      printf("------------ Monothread --------------\n");
      mem::ThreadDedicatedContext context;
      test_perf_alloc();
   }
   if (1) {
      printf("------------ Cross-context --------------\n");
      mem::Regions.SetMaxUsablePhysicalBytes( size_t(1) << 31);
      size_t limit = mem::Regions.GetMaxUsablePhysicalBytes() * 0.50;
      size_t szblk = mem::cst::ObjectLayoutBase[4].object_multiplier;
      size_t count = limit / szblk;

      void** p = new void* [count];
      {
         mem::ThreadDedicatedContext context;
         for (size_t i = 0; i < count; i++) {
            p[i] = ins_malloc(szblk);
         }
         context.Pop();
         mem::Controller.Print();
      }
      {
         mem::ThreadDedicatedContext context;
         for (size_t i = 0; i < count; i++) {
            ins_free(p[i]);
            p[i] = ins_malloc(szblk);
         }
         mem::Controller.Print();
         for (size_t i = 0; i < count; i++) {
            ins_free(p[i]);
         }
         delete p;
         mem::Controller.Print();
      }
   }
   if (0) {
      printf("------------ Multithread --------------\n");
      std::thread t1(
         []() {
            mem::ThreadDedicatedContext context;
            test_perf_alloc();
         }
      );
      std::thread t2(
         []() {
            mem::ThreadDedicatedContext context;
            test_perf_alloc();
         }
      );
      std::thread t3(
         []() {
            mem::ThreadDedicatedContext context;
            test_perf_alloc();
         }
      );
      std::thread t4(
         []() {
            mem::ThreadDedicatedContext context;
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
      mem::Regions.SetMaxUsablePhysicalBytes(size_t(1) << 30);
      size_t limit = mem::Regions.GetMaxUsablePhysicalBytes() * 0.8;
      size_t szblk = mem::cst::ObjectLayoutBase[4].object_multiplier;
      size_t count = limit / szblk;
      std::thread t1(
         [&]() {
            mem::ThreadDedicatedContext context;
            void** p = new void* [count];
            for (size_t i = 0; i < count; i++) {
               p[i] = ins_malloc(szblk);
            }
            for (size_t i = 0; i < count; i++) {
               ins_free(p[i]);
            }
            delete p;
            context.Pop();
         }
      );
      t1.join();
      std::thread t2(
         [&]() {
            mem::ThreadDedicatedContext context;
            for (size_t sz = 0; sz < limit; sz += szblk) {
               ins_malloc(szblk);
            }
         }
      );
      t2.join();
      mem::Controller.Print();
   }
   {
      mem::Controller.PerformMemoryCleanup();
      mem::Controller.Print();
   }

   return 0;
}

