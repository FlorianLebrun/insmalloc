#include <ins/memory/objects-pool.h>
#include <ins/memory/controller.h>
#include <ins/timing.h>

using namespace ins;
using namespace ins::mem;

void sObjectRegion::NotifyAvailables(bool managed) {
   if (this->owner) {
      this->owner->objects[this->layoutID].notifieds.Push(this);
   }
   else if (managed) {
      mem::Central->managed.objects[this->layoutID].notifieds.Push(this);
   }
   else {
      mem::Central->unmanaged.objects[this->layoutID].notifieds.Push(this);
   }
}

/**********************************************************************
*
*   ObjectCentralContext
*
***********************************************************************/

void ObjectCentralContext::Initialize(bool managed) {
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
      _INS_TRACE(printf("PushUsableRegion\n"));
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

void ObjectLocalContext::Initialize(MemoryContext* context, ObjectCentralContext* heap) {
   this->managed = heap->managed;
   this->context = context;
   this->heap = heap;
}

void ObjectLocalContext::Scavenge() {
   for (int layoutID = 0; layoutID < cst::ObjectLayoutCount; layoutID++) {
      auto& pool = this->objects[layoutID];
      auto& central = this->heap->objects[layoutID];

      // Scavenge owned regions before dump
      this->ScavengeNotifiedRegions(layoutID);

      // Clean usables regions
      pool.usables.CollectDisposables(pool.disposables);

      // Dump all not full to central pool
      std::lock_guard<std::mutex> guard(central.lock);
      pool.usables.DumpInto(central.usables, 0);
      pool.disposables.DumpInto(central.disposables, 0);
   }
}

ObjectHeader ObjectLocalContext::AllocateObject(size_t size) {
   auto objectLayoutID = getLayoutForSize(size);
   _ASSERT(mem::cst::ObjectLayoutBase[objectLayoutID].object_multiplier == 0
      || size <= mem::cst::ObjectLayoutBase[objectLayoutID].object_multiplier
   );
   if (objectLayoutID < cst::ObjectLayoutMax) {
      return this->AcquireObject(objectLayoutID);
   }
   else {
      return this->AllocateLargeObject(size);
   }
}

ObjectHeader ObjectLocalContext::AllocateLargeObject(size_t size) {
   auto region = sObjectRegion::New(this->managed, cst::ObjectLayoutMax, size, this);
   auto obj = region->GetObjectAt(0);
   region->availables = 0;
   _ASSERT(ObjectLocation(&obj[1]).IsAlive());
   return obj;
}

ObjectHeader ObjectLocalContext::AllocateInstrumentedObject(size_t size, ObjectAllocOptions options) {
   size_t requiredSize = size + options.enableSecurityPadding;
   if (options.enableAnalytics) requiredSize += sizeof(ObjectAnalyticsInfos);
   auto objectLayoutID = getLayoutForSize(requiredSize);

   // Allocate memory
   ObjectHeader obj = 0;
   if (objectLayoutID < cst::ObjectLayoutMax) {
      obj = this->AcquireObject(objectLayoutID);
   }
   else {
      obj = this->AllocateLargeObject(size);
   }

   // Configure analytics infos
   uint32_t bufferLen = mem::cst::ObjectLayoutBase[objectLayoutID].object_multiplier;
   if (options.enableAnalytics) {
      bufferLen -= sizeof(ObjectAnalyticsInfos);
      auto infos = (ObjectAnalyticsInfos*)&ObjectBytes(obj)[bufferLen];
      infos->timestamp = timing::getCurrentTimestamp();
      infos->stackstamp = 42;
      obj->has_analytics_infos = 1;
   }

   // Configure security padding
   if (options.enableSecurityPadding) {
      uint8_t* paddingBytes = &ObjectBytes(obj)[size];
      uint32_t paddingLen = bufferLen - size - sizeof(uint32_t);
      uint32_t& paddingTag = (uint32_t&)paddingBytes[paddingLen];
      paddingTag = size ^ 0xabababab;
      memset(paddingBytes, 0xab, paddingLen);
      obj->has_security_padding = 1;
   }

   return obj;
}

uint32_t ObjectLocalContext::ScavengeNotifiedRegions(uint8_t layoutID) {
   return this->ScavengeNotifiedRegions(this->objects[layoutID].notifieds.Flush());
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
   auto& pool = this->objects[layoutID];
   pool.disposables.Push(region);
   /*if (disposables.count > disposables.limit) {
      this->heap->ReceiveDisposables(layoutID, disposables);
   }*/
}

void ObjectLocalContext::PushUsableRegion(ObjectRegion region) {
   auto& pool = this->objects[region->layoutID];
   if (region->next.used == none<sObjectRegion>()) {
      if (pool.usables.count > 1 && region->IsDisposable()) {
         this->PushDisposableRegion(region->layoutID, region);
      }
      else {
         _INS_TRACE(printf("PushUsableRegion\n"));
         pool.usables.Push(region);
      }
   }
   else {
      _INS_TRACE(printf("overpush\n"));
   }
}

ObjectRegion ObjectLocalContext::PullUsableRegion(uint8_t layoutID) {
   auto& pool = this->objects[layoutID];
   _ASSERT(!pool.usables.current || pool.usables.current->availables == 0);
   pool.usables.Pop(); // Remove full current region
   if (pool.usables.current) {
      // @TODO: Perf: this loop shall be optimized
      while (pool.usables.count > 0 && pool.usables.current->IsDisposable()) {
         this->PushDisposableRegion(layoutID, pool.usables.Pop());
      }
   }
   else {
      auto new_region = pool.disposables.Pop();
      if (auto new_region = pool.disposables.Pop()) {
         pool.usables.Push(new_region);
      }
      else  if (this->ScavengeNotifiedRegions(layoutID)) {
         _ASSERT(pool.usables.current);
      }
      else {
         _ASSERT(!pool.usables.current);
         pool.usables.Push(sObjectRegion::New(this->managed, layoutID, this));
      }
   }
   _INS_ASSERT(pool.usables.current->availables != 0);
   return pool.usables.current;
}

__declspec(noinline) ObjectHeader ObjectLocalContext::AcquireObject(uint8_t layoutID) {
   auto& pool = this->objects[layoutID];

   // Acquire region with available objects
   auto region = pool.usables.current;
   if (!region) {
      region = this->PullUsableRegion(layoutID);
   }
   _INS_ASSERT(region->layoutID == layoutID);
   _INS_ASSERT(region->availables != 0);

   // Get an object index
   auto index = bit::lsb_64(region->availables);

   // Prepare new object
   auto offset = cst::ObjectLayoutBase[layoutID].GetObjectOffset(index);
   auto obj = ObjectHeader(&ObjectBytes(region)[offset]);
   _ASSERT(index == cst::ObjectLayoutBase[layoutID].GetObjectIndex(offset));
   _ASSERT(index == cst::ObjectLayoutBase[layoutID].GetObjectIndex(offset + cst::ObjectLayoutBase[layoutID].object_multiplier - 1));

   // Publish object as ready
   auto bit = uint64_t(1) << index;
   if ((region->availables ^= bit) == 0) {
      pool.usables.Pop();
   }

   _ASSERT(ObjectLocation(&obj[1]).IsAlive());
   return obj;
}
