#include <ins/memory/objects-context.h>
#include <ins/memory/space.h>

using namespace ins;

/**********************************************************************
*
*   ObjectClassHeap
*
***********************************************************************/

void ObjectClassHeap::Initiate(uint8_t layout) {
   this->layout = layout;
}

void ObjectClassHeap::Clean() {
   std::lock_guard<std::mutex> guard(lock);
   while (auto obj = this->shared_objects_cache.PopObject()) {
      auto region = (ObjectRegion)MemorySpace::GetRegionDescriptor(obj);
      _ASSERT(region->layout == this->layout);
      if (region->objects_bin.PushObject(obj, region)) {
         this->regions_pool.PushUsableRegion(region);
      }
   }
   this->regions_pool.CleanDisposables();
   while (auto region = this->regions_pool.disposables.PopRegion()) {
      region->Dispose();
      this->regions_pool.owneds_count--;
   }
}

void ObjectClassHeap::CheckValidity() {
   std::lock_guard<std::mutex> guard(lock);
   this->regions_pool.CheckValidity();
}

bool ObjectClassHeap::AcquireObjectBatch(ObjectBucket& bucket) {
   std::lock_guard<std::mutex> guard(lock);

   // Try alloc in objects bucket
   if (this->shared_objects_cache.TransfertBatchInto(bucket)) {
      return true;
   }

   // Try alloc in regions pool
   if (this->regions_pool.AcquireObjectBatch(bucket)) {
      return true;
   }

   // Acquire and alloc in a new region
   if (auto region = sObjectRegion::New(layout)) {
      region->owner = this;
      this->regions_pool.AddNewRegion(region);
      return this->regions_pool.AcquireObjectBatch(bucket);
   }

   return false;
}

bool ObjectClassHeap::DischargeObjectBucket(ObjectBucket& bucket) {
   std::lock_guard<std::mutex> guard(lock);
   return bucket.TransfertBatchInto(this->shared_objects_cache);
}

void ObjectClassHeap::DisposeObjectBucket(ObjectBucket& bucket) {
   std::lock_guard<std::mutex> guard(lock);
   bucket.DumpInto(this->shared_objects_cache);
}


/**********************************************************************
*
*   ObjectClassContext
*
***********************************************************************/

void ObjectClassContext::Initiate(ObjectClassHeap* heap) {
   _ASSERT(ObjectLayoutInfos[heap->layout].policy == SmallSlabPolicy);
   this->heap = heap;
   this->layout = heap->layout;
}

void ObjectClassContext::Clean() {
   this->heap->DisposeObjectBucket(this->shared_objects_cache);
   this->regions_pool.DumpInto(this->heap->regions_pool, this->heap);
   this->CheckValidity();
}

void ObjectClassContext::CheckValidity() {
   this->regions_pool.CheckValidity();
}
