#include <stdint.h>
#include <semaphore>
#include <stdlib.h>
#include <ins/memory/objects.h>
#include <ins/memory/space.h>

using namespace ins;

struct ObjectClassContext {
   virtual ObjectHeader alloc() = 0;
   virtual void free(ObjectHeader obj) = 0;
};

struct ObjectListPoolContext : ObjectClassContext {
   ObjectClassHeap* heap;
   ObjectListBucket bucket;
   uint8_t layout;

   ObjectListPoolContext(ObjectClassHeap* heap) : layout(heap->layout) {
      _ASSERT(ObjectLayoutInfos[layout].policy == SmallSlabPolicy);
      this->heap = heap;
   }

   ObjectHeader alloc() override {

      // Try alloc in context bucket
      if (auto obj = bucket.AcquireObject()) return obj;

      // Acquire and alloc in a new region
      if (auto lst = heap->AcquireList()) {
         bucket.DisposeList(lst);
         return bucket.AcquireObject();
      }

      return 0;
   }
   void free(ObjectHeader obj) override {
      auto region = (ObjectRegion)space.GetRegionDescriptor(obj);
      _ASSERT(region->layout == this->layout);
      if (region->owner == this->heap) {
         if (auto lst = bucket.DisposeObject(obj)) {
            this->heap->DiposeList(lst);
         }
      }
      else {
         printf("TODO\n");
      }
   }
};

struct ObjectRegionPoolContext : ObjectClassContext {
   ObjectClassHeap* heap;
   ObjectRegionBucket regions;
   ObjectListBucket bucket;
   uint8_t layout;

   ObjectRegionPoolContext(ObjectClassHeap* heap) : layout(heap->layout) {
      _ASSERT(ObjectLayoutInfos[layout].policy == SmallSlabPolicy);
      this->heap = heap;
   }

   ObjectHeader alloc() override {

      // Try alloc in context bucket
      if (auto obj = bucket.AcquireObject()) return obj;

      // Try alloc in regions bucket
      if (auto obj = regions.AcquireObject()) return obj;

      // Acquire and alloc in a new region
      if (auto region = sObjectRegion::New(layout)) {
         region->owner = this;
         regions.AddRegion(region);
         return regions.AcquireObject();
      }

      return 0;
   }
   void free(ObjectHeader obj) override {
      auto region = (ObjectRegion)space.GetRegionDescriptor(obj);
      _ASSERT(region->layout == this->layout);
      if (region->owner) {
         auto context = (ObjectRegionPoolContext*)region->owner;
         if (region->owner == this) {
            if (auto lst = region->bucket.DisposeObject(obj)) {
               printf("TODO\n");
            }
         }
         else if (auto lst = bucket.DisposeObject(obj)) {
            printf("TODO\n");
         }
      }
      else {
         if (auto lst = bucket.DisposeObject(obj)) {
            printf("TODO\n");
         }
      }
   }
};

sObjectRegion* sObjectRegion::New(uint8_t layout) {
   auto& infos = ObjectLayoutInfos[layout];
   auto region = new(space.AllocateRegion(infos.region_sizeL2)) sObjectRegion(layout);
   space.SetRegionEntry(region, RegionEntry::ObjectRegion(layout));
   return region;
}

ObjectHeader ObjectListBucket::AcquireObject() {
   if (auto obj = reserve) {
      if (obj->nextObject) {
         reserve = obj->nextObject;
      }
      else {
         reserve = obj->nextList;
         this->listcount--;
      }
      obj->used = 1;
      return obj;
   }
   return 0;
}

ObjectChain ObjectListBucket::DisposeObject(ObjectHeader obj) {
   ObjectChain freelist = 0;
   auto cobj = ObjectChain(obj);
   obj->used = 0;
   if (reserve && reserve->length < maxObjectPerList) {
      cobj->nextObject = reserve;
      cobj->nextList = 0;
      cobj->length = reserve->length + 1;
   }
   else {
      if (this->listcount >= maxListCount) {
         freelist = reserve;
         reserve = freelist->nextList;
      }
      else {
         this->listcount++;
      }
      cobj->nextObject = 0;
      cobj->nextList = reserve;
      cobj->length = 0;
   }
   this->reserve = cobj;
   return freelist;
}

ObjectChain ObjectListBucket::AcquireList() {
   if (ObjectChain freelist = reserve) {
      reserve = freelist->nextList;
      return freelist;
   }
   return 0;
}

ObjectChain ObjectListBucket::DisposeList(ObjectChain list) {
   if (this->listcount < maxListCount) {
      if (reserve) {
         list->nextList = reserve->nextList;
         reserve->nextList = list;
      }
      else {
         list->nextList = 0;
         reserve = list;
      }
      this->listcount++;
      return 0;
   }
   else {
      return list;
   }
}

#include <vector>

void test_small_object() {
   ObjectClassHeap heap(4);
   ObjectRegionPoolContext context(&heap);
   auto obj = context.alloc();
   context.free(obj);

   size_t count = 2000;
   std::vector<ObjectHeader> objects;
   for (int i = 0; i < count; i++) {
      objects.push_back(0);
   }
   for (int i = 0; i < count; i++) {
      printf("! acquire %d\n", i);
      auto obj = context.alloc();
      objects[i] = obj;
   }
   for (int i = 0; i < count; i++) {
      printf("! dispose %d\n", i);
      context.free(obj);
   }

   space.Print();
}
