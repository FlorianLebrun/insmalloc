#pragma once
#include <ins/memory/objects.h>
#include <ins/memory/objects-region.h>

namespace ins {

   struct ObjectClassProvider : public IObjectRegionOwner {
      uint8_t layout = 0;
      ObjectRegionPool regions_pool;
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

      void Initiate(uint8_t layout);
      void Clean();

      bool AcquireObjectBatch(ObjectBucket& bucket);
      bool DischargeObjectBucket(ObjectBucket& bucket);
      void DisposeObjectBucket(ObjectBucket& bucket);

      void CheckValidity();
   };

   /**********************************************************************
   *
   *   Object Layout Context
   *   (monothreaded memory context of an object class)
   *
   ***********************************************************************/
   struct ObjectClassContext : public ObjectClassProvider {
      ObjectClassHeap* heap = 0;
      ObjectBucket shared_objects_cache;

      void Initiate(ObjectClassHeap* heap);
      void Clean();
      void CheckValidity();

      ObjectHeader AllocatePrivateObject();
      ObjectHeader AllocateSharedObject();
      void FreeObject(ObjectHeader obj, ObjectRegion region);
   };

   inline ObjectHeader ObjectClassContext::AllocatePrivateObject() {

      // Try alloc in regions pool
      if (auto obj = this->regions_pool.AcquireObject()) {
         obj->used = 1;
         return obj;
      }

      // Acquire and alloc in a new region
      if (auto region = sObjectRegion::New(layout)) {
         region->owner = this;
         this->regions_pool.AddNewRegion(region);
         if (auto obj = this->regions_pool.AcquireObject()) {
            obj->used = 1;
            return obj;
         }
      }

      return 0;
   }

   inline ObjectHeader ObjectClassContext::AllocateSharedObject() {
      do {

         // Try alloc in context objects bucket
         if (auto obj = this->shared_objects_cache.PopObject()) {
            obj->used = 1;
            return obj;
         }

         // Fill a object cache from heap and retry
      } while (this->heap->AcquireObjectBatch(this->shared_objects_cache));

      return 0;
   }

   inline void ObjectClassContext::FreeObject(ObjectHeader obj, ObjectRegion region) {
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
         if (this->shared_objects_cache.batch_count > 2) {
            this->heap->DischargeObjectBucket(this->shared_objects_cache);
         }
      }
      else if (region->owner) {
         region->DisposeExchangedObject(obj);
      }
      else {
         printf("Cannot free object in not owned region\n");
      }
   }

}
