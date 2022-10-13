#include <ins/memory/space.h>
#include <ins/memory/heap.h>
#include <stdio.h>
#include <vector>
#include "./test_perf_alloc.h"

using namespace ins;

ins::MemoryContext* ins_malloc_handler::context = 0;

namespace SlabbedPolicy {
   void test_descriptor_region() {

      struct TestExtDesc : Descriptor {
         size_t commited;
         size_t size;

         TestExtDesc(size_t commited, size_t size) : size(size), commited(commited) {
            for (int i = sizeof(*this); i < commited; i++) ((char*)this)[i] = 0;
            this->Resize(size);
            for (int i = sizeof(*this); i < size; i++) ((char*)this)[i] = 0;
         }
         virtual size_t GetSize() {
            return this->size;
         }
         virtual void SetUsedSize(size_t commited) {
            this->commited = commited;
         }
         virtual size_t GetUsedSize() {
            return this->commited;
         }
      };
      auto desc = Descriptor::NewExtensible<TestExtDesc>(1000000);
      desc->Dispose();

      struct TestDesc : Descriptor {
         virtual size_t GetSize() {
            return sizeof(*this);
         }
      };

      std::vector<Descriptor*> descs;
      for (int i = 0; i < 10000; i++) {
         auto desc = Descriptor::New<TestDesc>();
         descs.push_back(desc);
      }
      for (auto desc : descs) {
         desc->Dispose();
      }
   }

   void apply_cross_context_private_alloc(SlabbedObjectContext& contextAlloc, SlabbedObjectContext& contextDispose) {
      {
         auto obj = contextAlloc.AllocatePrivateObject();
         contextDispose.FreeObject(obj, SlabbedObjectRegion(MemorySpace::GetRegionDescriptor(obj)));
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
               auto obj = objects[i];
               contextDispose.FreeObject(obj, SlabbedObjectRegion(MemorySpace::GetRegionDescriptor(obj)));
            }
            _INS_DEBUG(MemorySpace::Print());
            contextAlloc.CheckValidity();
         }
         contextAlloc.CheckValidity();
      }
   }


   void apply_cross_context_shared_alloc(SlabbedObjectContext& contextAlloc, SlabbedObjectContext& contextDispose) {
      {
         auto obj = contextAlloc.AllocateSharedObject();
         contextDispose.FreeObject(obj, SlabbedObjectRegion(MemorySpace::GetRegionDescriptor(obj)));
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
               auto obj = objects[i];
               contextDispose.FreeObject(obj, SlabbedObjectRegion(MemorySpace::GetRegionDescriptor(obj)));
            }
            _INS_DEBUG(MemorySpace::Print());
            contextAlloc.CheckValidity();
         }
         contextAlloc.CheckValidity();
      }
      {
         size_t count = 2000000;
         for (int i = 0; i < count; i++) {
            auto obj = contextAlloc.AllocateSharedObject();
            contextDispose.FreeObject(obj, SlabbedObjectRegion(MemorySpace::GetRegionDescriptor(obj)));
         }
         _INS_DEBUG(MemorySpace::Print());
         contextAlloc.CheckValidity();
      }
   }

   void test_private_object(MemoryHeap& heap) {
      auto contextA = heap.AcquireContext();
      auto contextB = heap.AcquireContext();
      int classId = 4;

      SlabbedObjectHeap& heapObj = heap.objects_slabbed[classId];
      SlabbedObjectContext& contextObjA = contextA->objects_slabbed[classId];
      SlabbedObjectContext& contextObjB = contextB->objects_slabbed[classId];

      apply_cross_context_private_alloc(contextObjA, contextObjA);
      apply_cross_context_private_alloc(contextObjA, contextObjB);

      heap.DisposeContext(contextA);
      heap.DisposeContext(contextB);
   }

   void test_shared_object(MemoryHeap& heap) {
      auto contextA = heap.AcquireContext();
      auto contextB = heap.AcquireContext();
      int classId = 4;

      SlabbedObjectHeap& heapObj = heap.objects_slabbed[classId];
      SlabbedObjectContext& contextObjA = contextA->objects_slabbed[classId];
      SlabbedObjectContext& contextObjB = contextB->objects_slabbed[classId];

      apply_cross_context_shared_alloc(contextObjA, contextObjA);
      apply_cross_context_shared_alloc(contextObjA, contextObjB);

      heap.DisposeContext(contextA);
      heap.DisposeContext(contextB);
   }

}

int main() {
   MemoryHeap& heap = ins_get_heap();
   //ins::generate_objects_layout_config("C:/git/project/insmalloc/lib/ins.memory.space");

   ins_malloc(64000);

   if (1) {
      heap.SetTimeStampOption(true);
      heap.SetStackStampOption(true);
      heap.SetSecurityPaddingOption(60);
   }
   extern void test_schemas();
   test_schemas();

   {
      ins::ObjectAnalyticsInfos meta;
      ins_malloc(50);
      ins_malloc(50);
      auto p = ins_malloc(50);
      auto p_sz = ins_msize(p);
      if (ins_get_metadata(p, meta)) {
         printf("");
      }
      ins_free(p);
      MemorySpace::Print();
   }

   test_perf_alloc();
   return 0;

   SlabbedPolicy::test_private_object(heap);
   SlabbedPolicy::test_shared_object(heap);
   //test_descriptor_region();

   heap.Clean();
   heap.CheckValidity();
   MemorySpace::Print();
   return 0;
}

