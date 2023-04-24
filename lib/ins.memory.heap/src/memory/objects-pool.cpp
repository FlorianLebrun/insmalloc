#include <ins/memory/objects-pool.h>
#include <ins/memory/space.h>
#include <ins/memory/controller.h>

using namespace ins;
using namespace ins::mem;

void sObjectRegion::NotifyAvailables(bool managed) {
   if (this->owner) {
      if (this->privated) {
         this->owner->privateds[this->layoutID].notifieds.Push(this);
      }
      else {
         this->owner->shareds[this->layoutID].notifieds.Push(this);
      }
   }
   else {
      if (managed) {
         mem::Controller.central.managed.objects[this->layoutID].notifieds.Push(this);
      }
      else {
         mem::Controller.central.unmanaged.objects[this->layoutID].notifieds.Push(this);
      }
   }
}

/**********************************************************************
*
*   ObjectCentralContext
*
***********************************************************************/

void ObjectCentralContext::Initiate(bool managed) {
   this->managed = managed;
}

void ObjectCentralContext::CheckValidity() {
   for (int layoutID = 0; layoutID < cst::ObjectLayoutCount; layoutID++) {
      auto& pool = this->objects[layoutID];
      std::lock_guard<std::mutex> guard(pool.lock);
      pool.usables.CheckValidity();
      pool.disposables.CheckValidity();
   }
}

void ObjectCentralContext::Clean() {
   for (int layoutID = 0; layoutID < cst::ObjectLayoutCount; layoutID++) {
      auto& pool = this->objects[layoutID];
      std::lock_guard<std::mutex> guard(pool.lock);
      pool.usables.CollectDisposables(pool.disposables);
      this->ScavengeNotifiedRegions(layoutID);
      pool.disposables.DisposeAll();
   }
}

void ObjectCentralContext::PushDisposableRegion(ObjectRegion region) {
   auto& disposables = this->objects[region->layoutID].disposables;
   disposables.Push(region);
}

void ObjectCentralContext::PushUsableRegion(ObjectRegion region) {
   _ASSERT(region->next.used == none<sObjectRegion>());
   auto& usables = this->objects[region->layoutID].usables;
   if (usables.count > 1 && region->IsDisposable()) {
      this->PushDisposableRegion(region);
   }
   else {
      _INS_DEBUG(printf("PushUsableRegion\n"));
      usables.Push(region);
   }
}

bool ObjectCentralContext::ScavengeNotifiedRegions(uint8_t layoutID) {
   uint32_t collecteds = 0;
   ObjectRegion region = this->objects[layoutID].notifieds.Flush();
   while (region) {
      auto next_region = region->next.notified;
      region->next.notified = none<sObjectRegion>();
      if (region->owner == 0) {
         auto notified_bits = region->notified_availables.exchange(0, std::memory_order_seq_cst);
         _ASSERT(notified_bits != 0);
         region->availables |= notified_bits;
         if (region->next.used == none<sObjectRegion>()) {
            this->PushUsableRegion(region);
         }
      }
      else {
         // Region mis routed (another context/thread using it)
         printf("! Redo region NotifyAvailables to good owner !\n");
         region->NotifyAvailables(this->managed);
      }
      region = next_region;
   }
   return collecteds > 0;
}

void ObjectCentralContext::ReceiveDisposables(uint8_t layoutID, ObjectRegionList& disposables) {
   auto& central = this->objects[layoutID];
   std::lock_guard<std::mutex> guard(central.lock);
   disposables.DumpInto(central.disposables, 0);
}

/**********************************************************************
*
*   ObjectLocalContext
*
***********************************************************************/

bool ObjectLocalContext::ScavengeNotifiedRegions(uint8_t layoutID) {
   uint32_t collecteds = this->ScavengeNotifiedRegions(this->privateds[layoutID].notifieds.Flush());
   collecteds += this->ScavengeNotifiedRegions(this->shareds[layoutID].notifieds.Flush());
   return collecteds > 0;
}

uint32_t ObjectLocalContext::ScavengeNotifiedRegions(ObjectRegion region) {
   uint32_t collecteds = 0;
   while (region) {
      auto next_region = region->next.notified;
      region->next.notified = none<sObjectRegion>();
      if (region->owner == this) {
         auto notified_bits = region->notified_availables.exchange(0, std::memory_order_seq_cst);
         _ASSERT(notified_bits != 0);
         region->availables |= notified_bits;
         this->PushUsableRegion(region);
         collecteds++;
      }
      else {
         // Region mis routed (another context/thread using it)
         printf("! Redo region NotifyAvailables to good owner !\n");
         region->NotifyAvailables(this->managed);
      }
      region = next_region;
   }
   return collecteds;
}

void ObjectLocalContext::PushDisposableRegion(uint8_t layoutID, ObjectRegion region) {
   auto& disposables = this->disposables[layoutID];
   disposables.Push(region);
   /*if (disposables.count > disposables.limit) {
      this->heap->ReceiveDisposables(layoutID, disposables);
   }*/
}

void ObjectLocalContext::PushUsableRegion(ObjectRegion region) {
   auto& pool = region->privated ? this->privateds[region->layoutID] : this->shareds[region->layoutID];
   if (region->next.used == none<sObjectRegion>()) {
      if (pool.usables.count > 1 && region->IsDisposable()) {
         this->PushDisposableRegion(region->layoutID, region);
      }
      else {
         _INS_DEBUG(printf("PushUsableRegion\n"));
         pool.usables.Push(region);
      }
   }
   else {
      _INS_DEBUG(printf("overpush\n"));
   }
}

bool ObjectLocalContext::PullActivePrivatedRegion(uint8_t layoutID) {
   auto& pool = this->privateds[layoutID];
   _ASSERT(pool.active_region == 0);
   auto nextRegion = pool.usables.Pop();
   if (nextRegion) {
      // @TODO: Perf: this loop shall be optimized
      while (pool.usables.count > 0 && nextRegion->IsDisposable()) {
         this->PushDisposableRegion(layoutID, nextRegion);
         nextRegion = pool.usables.Pop();
      }
   }
   else {
      nextRegion = this->disposables[layoutID].Pop();
      if (!nextRegion) {
         if (this->ScavengeNotifiedRegions(layoutID)) {
            auto nextRegion = pool.usables.Pop();
            if (!nextRegion) {
               nextRegion = this->disposables[layoutID].Pop();
            }
         }
         if (!nextRegion) {
            nextRegion = sObjectRegion::New(this->managed, layoutID, this);
            nextRegion->privated = 1;
         }
      }
   }
   pool.active_region = nextRegion;
   return true;
}

__declspec(noinline) ObjectHeader ObjectLocalContext::AcquirePrivatedObject(uint8_t layoutID) {
   auto& pool = this->privateds[layoutID];
   do {
      if (auto region = pool.active_region) {
         _ASSERT(region->layoutID == layoutID);
         // Get an object index
         auto index = bit::lsb_64(region->availables);

         // Prepare new object
         auto offset = cst::ObjectLayoutBase[layoutID].GetObjectOffset(index);
         auto obj = ObjectHeader(&ObjectBytes(region)[offset]);

         // Publish object as ready
         auto bit = uint64_t(1) << index;
         if ((region->availables ^= bit) == 0) {
            pool.active_region = 0;
         }

         return obj;
      }
   } while (this->PullActivePrivatedRegion(layoutID));
   return 0;
}

bool ObjectLocalContext::PullActiveSharedRegion(uint8_t layoutID) {
   auto& pool = this->shareds[layoutID];
   _ASSERT(pool.active_region == 0);
   auto nextRegion = pool.usables.Pop();
   if (nextRegion) {
      // @TODO: Perf: this loop shall be optimized
      while (pool.usables.count > 0 && nextRegion->IsDisposable()) {
         this->PushDisposableRegion(layoutID, nextRegion);
         nextRegion = pool.usables.Pop();
      }
   }
   else {
      nextRegion = this->disposables[layoutID].Pop();
      if (!nextRegion) {
         if (this->ScavengeNotifiedRegions(layoutID)) {
            auto nextRegion = pool.usables.Pop();
            if (!nextRegion) {
               nextRegion = this->disposables[layoutID].Pop();
            }
         }
         if (!nextRegion) {
            nextRegion = sObjectRegion::New(this->managed, layoutID, this);
            nextRegion->privated = 0;
         }
      }
   }
   pool.active_region = nextRegion;
   return true;
}

__declspec(noinline) ObjectHeader ObjectLocalContext::AcquireSharedObject(uint8_t layoutID) {
   auto& pool = this->shareds[layoutID];
   do {
      if (auto region = pool.active_region) {

         // Get an object index
         auto index = bit::lsb_64(region->availables);

         // Prepare new object
         auto offset = cst::ObjectLayoutBase[layoutID].GetObjectOffset(index);
         auto obj = ObjectHeader(&ObjectBytes(region)[offset]);

         // Publish object as ready
         auto bit = uint64_t(1) << index;
         if ((region->availables ^= bit) == 0) {
            pool.active_region = 0;
         }

         return obj;
      }
   } while (this->PullActiveSharedRegion(layoutID));
   return 0;
}
