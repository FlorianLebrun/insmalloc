#include <stdint.h>
#include <semaphore>
#include <stdlib.h>
#include <ins/memory/objects-base.h>
#include <ins/memory/contexts.h>
#include <ins/memory/controller.h>

using namespace ins;
using namespace ins::mem;

/**********************************************************************
*
*   Object Region
*
***********************************************************************/

sObjectRegion* sObjectRegion::New(bool managed, uint8_t layoutID, ObjectLocalContext* owner) {
   auto& infos = cst::ObjectLayoutInfos[layoutID];

   auto ptr = managed
      ? mem::AllocateManagedRegion(infos.region_sizeL2, infos.region_sizingID, owner->context)
      : mem::AllocateUnmanagedRegion(infos.region_sizeL2, infos.region_sizingID, owner->context);

   auto region = new(ptr) sObjectRegion(layoutID, size_t(1) << infos.region_sizeL2, owner);
   RegionLocation::New(region).layout() = layoutID;
   return region;
}

sObjectRegion* sObjectRegion::New(bool managed, uint8_t layoutID, size_t size, ObjectLocalContext* owner) {

   void* ptr = managed
      ? mem::AllocateManagedRegionEx(size + sizeof(sObjectRegion), owner->context)
      : mem::AllocateUnmanagedRegionEx(size + sizeof(sObjectRegion), owner->context);

   auto region = new(ptr) sObjectRegion(layoutID, size, owner);
   RegionLocation::New(ptr).layout() = layoutID;
   return region;
}

void sObjectRegion::DisplayToConsole() {
   auto& infos = cst::ObjectLayoutInfos[this->layoutID];

   address_t addr(this);
   auto region_size = size_t(1) << infos.region_sizeL2;
   printf("\n%X%.8llX %s:", int(addr.arenaID), int64_t(addr.position), sz2a(region_size).c_str());

   auto nobj = this->GetAvailablesCount();
   auto nobj_max = infos.region_objects;
   printf(" layout(%d) objects(%d/%d)", this->layoutID, int(nobj), int(nobj_max));
   printf(" owner(%p)", this->owner);
   if (this->IsDisposable()) printf(" [empty]");
}

void sObjectRegion::Dispose() {
   auto& infos = cst::ObjectLayoutInfos[this->layoutID];
   mem::DisposeRegion(this, infos.region_sizeL2, infos.region_sizingID);
}

/**********************************************************************
*
*   Object Location
*
***********************************************************************/

bool mem::ObjectLocation::IsAlive() {
   if (!this->object) {
      return false;
   }
   else if (this->object->schema_id == sObjectSchema::InvalidateID) {
      return false;
   }
   else if (this->region->IsObjectAvailable(uint64_t(1) << this->index)) {
      return false;
   }
   return true;
}

bool mem::ObjectLocation::IsAllocated() {
   if (!this->object) {
      return false;
   }
   else if (this->region->IsObjectAvailable(uint64_t(1) << this->index)) {
      return false;
   }
   return true;
}

void mem::ObjectLocation::Retain() {
   _ASSERT(this->IsAlive());
   if (!this->object) return;

   auto& tag = (std::atomic_uint8_t&)this->object->hard_retention;
   uint8_t prev_tag;
   do {
      prev_tag = tag.load(std::memory_order_relaxed);
      if (prev_tag == 0xff) {
         throw "Hard counter overflow";
      }
   } while (!tag.compare_exchange_strong(prev_tag, prev_tag + 1));
}

void mem::ObjectLocation::RetainWeak() {
   _ASSERT(this->IsAlive());
   if (!this->object) return;

   auto& tag = (std::atomic_uint8_t&)this->object->weak_retention;
   uint8_t prev_tag;
   do {
      prev_tag = tag.load(std::memory_order_relaxed);
      if (prev_tag == 0xff) {
         throw "Weak counter overflow";
      }
   } while (!tag.compare_exchange_strong(prev_tag, prev_tag + 1));
}

bool mem::ObjectLocation::Release(MemoryContext* context) {
   if (!this->object) return false;
   _ASSERT(this->IsAlive());

   // Decremement counter
   auto& tag = (std::atomic_uint8_t&)this->object->hard_retention;
   uint8_t prev_tag;
   do {
      prev_tag = tag.load(std::memory_order_relaxed);
      if (prev_tag == 0x00) break;
   } while (!tag.compare_exchange_strong(prev_tag, prev_tag - 1));

   // Dispose object when is no hard retention
   if (prev_tag == 0x00) {
      auto desc = mem::GetObjectSchema(this->object->schema_id);
      if (desc->finalizer) {
         desc->finalizer(this->object);
      }
      if (this->object->weak_retention == 0) {
         this->Free(context);
         return true;
      }
      else {
         // Keep object buffer for pending weak retention
         this->object->schema_id = sObjectSchema::InvalidateID;
      }
   }
   return false;
}

bool mem::ObjectLocation::ReleaseWeak(MemoryContext* context) {
   if (!this->object) return false;
   _ASSERT(this->IsAllocated());

   // Decremement counter
   auto& tag = (std::atomic_uint8_t&)this->object->weak_retention;
   uint8_t prev_tag;
   do {
      prev_tag = tag.load(std::memory_order_relaxed);
      if (prev_tag == 0x00) {
         throw "Weak counter underflow";
      }
   } while (!tag.compare_exchange_strong(prev_tag, prev_tag - 1));

   // Free object when is invalidate
   if (prev_tag == 0x01 &&
      this->object->retention == 0 &&
      this->object->schema_id == sObjectSchema::InvalidateID)
   {
      this->Free(context);
      return true;
   }

   return false;
}

bool mem::ObjectLocation::Free(MemoryContext* context) {
   _ASSERT(!context || !context->isShared);
   if (!this->object) {
      mem::NotifyHeapIssue(tHeapIssue::FreeOutOfBoundObject, this->region);
      return false;
   }
   auto object_bit = uint64_t(1) << this->index;

   // Check object is not available
   if (region->IsObjectAvailable(object_bit)) {
      mem::NotifyHeapIssue(tHeapIssue::FreeInexistingObject, this->object);
      return false;
   }

   // Check object is not available
   if (this->object->retention) {
      mem::NotifyHeapIssue(tHeapIssue::FreeRetainedObject, this->object);
      return false;
   }

   // Release object to region owner
   auto owner = this->arena.managed ? &context->managed : &context->unmanaged;
   if (region->owner == owner) {
      if (region->availables == 0) {
         owner->PushUsableRegion(region);
      }
      region->availables |= object_bit;
      _ASSERT(region->availables != 0);
   }
   else {
      if (region->notified_availables.fetch_or(object_bit) == 0) {
         if (region->owner) {
            auto count = region->owner->objects[region->layoutID].notifieds.Push(region);
            if (count > 10) {
               mem::ScheduleContextRecovery(region->owner->context);
            }
         }
         else {
            auto list = this->arena.managed ? mem::Central->managed.objects : mem::Central->unmanaged.objects;
            list[region->layoutID].notifieds.Push(region);
         }
      }
   }
   return true;
}
