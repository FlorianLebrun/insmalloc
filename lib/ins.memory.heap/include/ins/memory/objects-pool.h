#pragma once
#include <ins/memory/objects-base.h>

namespace ins::mem {

   struct ObjectCentralContext;
   struct ObjectLocalContext;
   struct MemoryContext;
   struct MemoryCentralContext;

   union ObjectAllocOptions {
      struct {
         uint8_t enableTimeStamp : 1;
         uint8_t enableStackStamp : 1;
         uint8_t enableSecurityPadding;
      };
      struct {
         uint8_t enableAnalytics : 2;
      };
      uint16_t enableds = 0;
   };

   // Object Region List
   struct ObjectRegionList {
      ObjectRegion current = 0;
      ObjectRegion last = 0;
      uint32_t count = 0;
      uint32_t limit = 0;

      void Push(ObjectRegion region) {
         region->next.used = 0;
         if (!this->last) this->current = region;
         else this->last->next.used = region;
         this->last = region;
         this->count++;
      }
      ObjectRegion Pop() {
         if (auto region = this->current) {
            this->current = region->next.used;
            if (!this->current) this->last = 0;
            this->count--;
            region->next.used = none<sObjectRegion>();
            return region;
         }
         return 0;
      }
      void DisposeAll() {
         while (auto region = this->Pop()) {
            region->Dispose();
         }
      }
      void CollectDisposables(ObjectRegionList& disposables) {
         ObjectRegion* pregion = &this->current;
         ObjectRegion last = 0;
         while (*pregion) {
            auto region = *pregion;
            if (region->IsDisposable()) {
               *pregion = region->next.used;
               this->count--;
               disposables.Push(region);
            }
            else {
               last = region;
               pregion = &region->next.used;
            }
         }
         this->last = last;
      }
      void DumpInto(ObjectRegionList& receiver, ObjectLocalContext* owner) {
         if (this->current) {
            for (ObjectRegion region = this->current; region; region = region->next.used) {
               region->owner = owner;
            }
            if (receiver.last) {
               receiver.last->next.used = this->current;
               receiver.last = this->last;
               receiver.count += this->count;
            }
            else {
               receiver.current = this->current;
               receiver.last = this->last;
               receiver.count = this->count;
            }
            this->current = 0;
            this->last = 0;
            this->count = 0;
         }
      }
      void CheckValidity() {
         uint32_t c_count = 0;
         for (auto x = this->current; x && c_count < 1000000; x = x->next.used) {
            if (!x->next.used) _ASSERT(x == this->last);
            c_count++;
         }
         _ASSERT(c_count == this->count);
      }
   };

   // Object Notified Region List
   struct ObjectRegionNotifieds {
      std::atomic<uint64_t> list;
      uint64_t Push(ObjectRegion region) {
         _INS_TRACE(printf("PushNotifiedRegion\n"));
         _ASSERT(region->next.notified == none<sObjectRegion>());
         for (;;) {
            uint64_t current = this->list.load(std::memory_order_relaxed);
            uint64_t count = current & 0xffff;
            if (count < 1000) {
               count++;
            }
            else {
               //printf("Notified overflow\n");
            }
            uint64_t next = count | (uint64_t(region) << 16);
            region->next.notified = ObjectRegion(current >> 16);
            if (this->list.compare_exchange_weak(
               current, next,
               std::memory_order_release,
               std::memory_order_relaxed
            )) {
               return count;
            }
         }
      }
      ObjectRegion Flush() {
         uint64_t current = this->list.exchange(0);
         return ObjectRegion(current >> 16);
      }
      size_t Count() {
         uint64_t current = this->list.load(std::memory_order_relaxed);
         return current & 0xffff;
      }
   };

   /**********************************************************************
   *
   *   ObjectCentralContext
   *   (multithreaded memory context/heap of an object class)
   *
   ***********************************************************************/

   struct ObjectCentralContext {

      struct CentralObjects {
         std::mutex lock;

         // Used regions list
         ObjectRegionList usables; // Region with available objects
         ObjectRegionList disposables; // Region with no used objects

         // Notified region for checking
         ObjectRegionNotifieds notifieds;
      };

      bool managed = false;
      std::uint64_t objects_notifieds_warnings;
      CentralObjects objects[cst::ObjectLayoutCount];

      void Initialize(bool managed);
      void CheckValidity();
      void Clean();

      void PushDisposableRegion(ObjectRegion region);
      void PushUsableRegion(ObjectRegion region);

      bool ScavengeNotifiedRegions(uint8_t layoutID);
      void ReceiveDisposables(uint8_t layoutID, ObjectRegionList& disposables);
   };

   /**********************************************************************
   *
   *   ObjectLocalContext
   *   (monothreaded memory hap of an object class)
   *
   ***********************************************************************/

   struct ObjectLocalContext {

      struct ObjectPool {
         ObjectRegionList usables; // Usable regions for next active
         ObjectRegionNotifieds notifieds; // Notified region for checking
         ObjectRegionList disposables; // Region with no used objects
      };

      bool managed = false;
      MemoryContext* context = 0;
      ObjectCentralContext* heap = 0;

      ObjectPool objects[cst::ObjectLayoutCount];

      void Initialize(MemoryContext* context, ObjectCentralContext* central);
      void Scavenge();

      ObjectHeader AllocateObject(size_t size);
      ObjectHeader AllocateLargeObject(size_t size);
      ObjectHeader AllocateInstrumentedObject(size_t size, ObjectAllocOptions options);

      void PushDisposableRegion(uint8_t layoutID, ObjectRegion region);
      void PushUsableRegion(ObjectRegion region);

   protected:
      ObjectHeader AcquireObject(uint8_t layoutID);

      ObjectRegion PullUsableRegion(uint8_t layoutID);

      uint32_t ScavengeNotifiedRegions(uint8_t layoutID);
      uint32_t ScavengeNotifiedRegions(ObjectRegion region);
   };
}
