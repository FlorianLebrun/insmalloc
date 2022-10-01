#pragma once
#include <ins/memory/objects.h>
#include <ins/memory/objects-region.h>
#include <ins/memory/space.h>

namespace ins {

   struct ObjectClassProvider : IObjectRegionOwner {
      uint8_t layout;
      ObjectRegionPool regions_pool;
      ObjectClassProvider(uint8_t layout)
         : layout(layout)
      {
      }
      void NotifyAvailableRegion(sObjectRegion* region) override final {
         this->regions_pool.PushNotifiedRegion(region);
      }
   };

   /**********************************************************************
   *
   *   Object Class Heap
   *   (multithreaded memory context/heap of an object class)
   *
   ***********************************************************************/
   struct ObjectClassHeap : ObjectClassProvider {
      ObjectBucket shared_objects_cache;
      std::mutex lock;

      ObjectClassHeap(uint8_t layout)
         : ObjectClassProvider(layout)
      {
      }

      bool AcquireObjectBatch(ObjectBucket& bucket) {
         std::lock_guard<std::mutex> guard(lock);

         // Try alloc in regions bucket
         if (this->regions_pool.AcquireObjectBatch(bucket)) {
            return true;
         }

         // Acquire and alloc in a new region
         if (auto region = sObjectRegion::New(layout)) {
            region->owner = this;
            this->regions_pool.PushUsableRegion(region);
            return this->regions_pool.AcquireObjectBatch(bucket);
         }

         return false;
      }

      bool TransfertBatch(ObjectBucket& bucket) {
         std::lock_guard<std::mutex> guard(lock);
         return bucket.TransfertBatch(this->shared_objects_cache);
      }
   };

   /**********************************************************************
   *
   *   Object Class Context 
   *   (monothreaded memory context of an object class)
   *
   ***********************************************************************/
   struct ObjectClassContext : public ObjectClassProvider {
      ObjectClassHeap* heap;
      ObjectBucket shared_objects_cache;

      ObjectClassContext(ObjectClassHeap* heap) : ObjectClassProvider(heap->layout) {
         _ASSERT(ObjectLayoutInfos[layout].policy == SmallSlabPolicy);
         this->heap = heap;
      }

      ObjectHeader AllocatePrivateObject() {

         // Try alloc in context bucket
         if (auto obj = this->regions_pool.AcquireObject()) {
            return obj;
         }

         // Acquire and alloc in a new region
         if (auto region = sObjectRegion::New(layout)) {
            region->owner = this;
            this->regions_pool.PushUsableRegion(region);
            return this->regions_pool.AcquireObject();
         }

         return 0;
      }

      ObjectHeader AllocateSharedObject() {

         // Try alloc in context bucket
         if (auto obj = this->shared_objects_cache.PopObject()) {
            return obj;
         }

         // Acquire and alloc in a new region
         if (this->heap->AcquireObjectBatch(this->shared_objects_cache)) {
            return this->shared_objects_cache.PopObject();
         }

         return 0;
      }

      void FreeObject(ObjectHeader obj) {
         auto region = (ObjectRegion)space.GetRegionDescriptor(obj);
         _ASSERT(region->layout == this->layout);
         _INS_PROTECT_CONDITION(obj->used == 1);
         obj->used = 0;
         if (region->owner == this) {
            if (region->objects_bin.PushObject(obj, region)) {
               this->regions_pool.PushUsableRegion(region);
            }
         }
         else if (region->owner == this->heap) {
            this->shared_objects_cache.PushObject(obj);
            if (this->shared_objects_cache.list_count > 2) {
               this->heap->TransfertBatch(this->shared_objects_cache);
            }
         }
         else if (region->owner) {
            region->DisposeExchangedObject(obj);
         }
         else {
            printf("Cannot free object in not owned region\n");
         }
      }

   };
}
