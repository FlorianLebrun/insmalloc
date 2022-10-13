#include <ins/memory/objects-slabbed.h>
#include <ins/memory/space.h>

using namespace ins;

void sSlabbedObjectRegion::DisplayToConsole() {
   address_t addr(this);
   auto region_size = size_t(1) << this->infos.region_sizeL2;
   printf("\n%X%.8X %lld bytes:", addr.arenaID, addr.position, region_size);

   auto nobj = this->GetAvailablesCount();
   auto nobj_max = this->infos.object_count;
   printf(" layout(%d) objects(%d/%d)", this->layout, nobj, nobj_max);
   printf(" owner(%p)", this->owner);
   if (this->IsDisposable()) printf(" [empty]");
}

/**********************************************************************
*
*   SlabbedRegionPool
*
***********************************************************************/

void SlabbedRegionPool::CheckValidity() {
   uint32_t c_known_empty_count = 0;

   this->ScavengeNotifiedRegions();

   this->usables.CheckValidity();
   for (auto x = this->usables.first; x; x = x->next.used) {
      if (x->IsDisposable()) c_known_empty_count++;
   }

   this->disposables.CheckValidity();
   for (auto x = this->disposables.first; x; x = x->next.used) {
      if (x->IsDisposable()) c_known_empty_count++;
   }

   _ASSERT(this->owneds_count >= this->usables.count + this->disposables.count);

   uint32_t c_regions_count = 0;
   uint32_t c_regions_empty_count = 0;
   MemorySpace::ForeachObjectRegion(
      [&](ObjectRegion region) {
         intptr_t offset = intptr_t(this) - intptr_t(region->owner);
         if (offset == offsetof(SlabbedObjectProvider, regions_pool)) {
            if (SlabbedObjectRegion(region)->IsDisposable()) {
               c_regions_empty_count++;
            }
            c_regions_count++;
         }
         return true;
      }
   );
   _ASSERT(c_regions_count == this->owneds_count);
   _ASSERT(c_regions_empty_count == c_known_empty_count);
}

/**********************************************************************
*
*   SlabbedObjectHeap
*
***********************************************************************/

void SlabbedObjectHeap::Initiate(uint8_t layout) {
   this->layout = layout;
}

void SlabbedObjectHeap::Clean() {
   std::lock_guard<std::mutex> guard(lock);
   while (auto obj = this->shared_objects_cache.PopObject()) {
      auto region = (SlabbedObjectRegion)MemorySpace::GetRegionDescriptor(obj);
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

void SlabbedObjectHeap::CheckValidity() {
   std::lock_guard<std::mutex> guard(lock);
   this->regions_pool.CheckValidity();
}

bool SlabbedObjectHeap::AcquireObjectBatch(ObjectBucket& bucket) {
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
   if (auto region = sSlabbedObjectRegion::New(layout)) {
      region->owner = this;
      this->regions_pool.AddNewRegion(region);
      return this->regions_pool.AcquireObjectBatch(bucket);
   }

   return false;
}

bool SlabbedObjectHeap::DischargeObjectBucket(ObjectBucket& bucket) {
   std::lock_guard<std::mutex> guard(lock);
   return bucket.TransfertBatchInto(this->shared_objects_cache);
}

void SlabbedObjectHeap::DisposeObjectBucket(ObjectBucket& bucket) {
   std::lock_guard<std::mutex> guard(lock);
   bucket.DumpInto(this->shared_objects_cache);
}


/**********************************************************************
*
*   SlabbedObjectContext
*
***********************************************************************/

void SlabbedObjectContext::Initiate(SlabbedObjectHeap* heap) {
   _ASSERT(ObjectLayoutInfos[heap->layout].policy == SlabbedObjectPolicy);
   this->heap = heap;
   this->layout = heap->layout;
}

void SlabbedObjectContext::Clean() {
   this->heap->DisposeObjectBucket(this->shared_objects_cache);
   this->regions_pool.DumpInto(this->heap->regions_pool, this->heap);
   this->CheckValidity();
}

void SlabbedObjectContext::CheckValidity() {
   this->regions_pool.CheckValidity();
}
