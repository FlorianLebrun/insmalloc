#include <ins/binary/alignment.h>
#include <ins/memory/space.h>
#include <ins/memory/structs.h>
#include <ins/memory/objects-context.h>
#include <stdio.h>
#include <vector>
#include <fstream>
#include <iostream>

using namespace ins;

void generate_layout_config(std::string path);

void test_descriptor_region() {

   auto region = sDescriptorAllocator::New(0);

   struct TestExtDesc : sDescriptor {
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
   auto desc = region->NewExtensible<TestExtDesc>(1000000);
   desc->Dispose();

   struct TestDesc : sDescriptor {
      virtual size_t GetSize() {
         return sizeof(*this);
      }
   };
   std::vector<Descriptor> descs;
   for (int i = 0; i < 10000; i++) {
      auto desc = region->New<TestDesc>();
      descs.push_back(desc);
   }
   for (auto desc : descs) {
      desc->Dispose();
   }
}

void apply_cross_context_private_alloc(ObjectClassContext& contextAlloc, ObjectClassContext& contextDispose) {

   {
      auto obj = contextAlloc.AllocatePrivateObject();
      contextDispose.FreeObject(obj);
   }

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
      space.Print();
      for (int i = 0; i < count; i++) {
         //printf("! dispose %d\n", i);
         auto obj = objects[i];
         contextDispose.FreeObject(obj);
      }
      space.Print();
   }
}


void apply_cross_context_shared_alloc(ObjectClassContext& contextAlloc, ObjectClassContext& contextDispose) {

   {
      auto obj = contextAlloc.AllocateSharedObject();
      contextDispose.FreeObject(obj);
   }

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
      space.Print();
      for (int i = 0; i < count; i++) {
         //printf("! dispose %d\n", i);
         auto obj = objects[i];
         contextDispose.FreeObject(obj);
      }
      space.Print();
   }

}

void test_private_object() {
   ObjectClassHeap heap(4);
   ObjectClassContext contextA(&heap);
   ObjectClassContext contextB(&heap);
   apply_cross_context_private_alloc(contextA, contextA);
   apply_cross_context_private_alloc(contextA, contextB);
}

void test_shared_object() {
   ObjectClassHeap heap(4);
   ObjectClassContext contextA(&heap);
   ObjectClassContext contextB(&heap);
   apply_cross_context_shared_alloc(contextA, contextA);
   //apply_cross_context_shared_alloc(contextA, contextB);
}

int main() {
   auto div = getBlockDivider(64);
   uint32_t p = 0x12560000 + getBlockIndexToOffset(64, 0, 0xffff);
   uint32_t i1 = getBlockOffsetToIndex(div, p - 64, 0xffff);
   uint32_t i2 = getBlockOffsetToIndex(div, p - 65, 0xffff);


   //generate_layout_config("C:/git/project/insmalloc/lib/ins.memory.space2");
   test_private_object();
   //test_shared_object();
   //test_descriptor_region();

   return 0;
}

