#pragma once
#include <ins/memory/objects.h>

namespace ins {

   /**********************************************************************
   *
   *   Object Region Buckets
   *
   ***********************************************************************/
   struct ObjectRegionBucket {
      struct Shared;

      struct Private {
      private:
         friend struct Shared;
         typedef struct sObjectChain {
            uint64_t __reserved_head;
            uint32_t next; // offset to next object
         } *ObjectChain;
         uint32_t first = 0;
         uint32_t last = 0; // Note: is clobbered when 'this->first' is nil
         uint32_t count = 0;
      public:
         uint32_t length() {
            return this->count;
         }
         bool PushObject(ObjectHeader obj, ObjectRegion owner) {
            auto cobj = ObjectChain(obj);
            cobj->next = this->first;
            if (cobj->next) this->last = this->first;
            this->first = uintptr_t(obj) - uintptr_t(owner);
            return this->count++ == 0;
         }
         ObjectHeader PopObject(ObjectRegion owner) {
            if (this->first) {
               auto cobj = ObjectChain(this->first + uintptr_t(owner));
               this->first = cobj->next;
               this->count--;
               return ObjectHeader(cobj);
            }
            return 0;
         }
         void DumpInto(Private& receiver, ObjectRegion owner) {
            if (this->count) {
               if (receiver.first) {
                  auto rlast = ObjectChain(receiver.last + uintptr_t(owner));
                  rlast->next = this->first;
               }
               else {
                  receiver.first = this->first;
               }
               receiver.last = this->last;
               receiver.count += this->count;

               this->first = 0;
               this->last = 0;
               this->count = 0;
            }
         }
         void CheckValidity() {
            int c = 0;
            auto base = uintptr_t(this) & ~uintptr_t(0x3ff);
            for (auto x = first; x; x = ObjectChain(x + base)->next) c++;
            _ASSERT(c == this->count);
         }
      };

      struct Shared {
      private:
         typedef struct sObjectChain {
            uint64_t __reserved_head;
            uint32_t next; // offset to next object
            uint32_t last; // offset to last object (only for shared)
         } *ObjectChain;
         union ChainEntry {
            struct {
               uint32_t first; // offset to next object
               uint32_t count;
            };
            uint64_t bits;
            ChainEntry(uint64_t bits) : bits(bits) {}
            ChainEntry(uint32_t first, uint32_t count) : first(first), count(count) {}
         };
         std::atomic<uint64_t> items = 0;
      public:
         uint32_t length() {
            return ChainEntry(this->items.load(std::memory_order_relaxed)).count;
         }
         bool PushObject(ObjectHeader obj, ObjectRegion owner) {
            auto cobj = ObjectChain(obj);
            for (;;) {
               ChainEntry cur(this->items.load(std::memory_order_relaxed));
               ChainEntry next(uintptr_t(obj) - uintptr_t(owner), cur.count + 1);
               if (cur.first) {
                  cobj->next = cur.first;
                  cobj->last = ObjectChain(uintptr_t(owner) + cur.first)->last;
               }
               else {
                  cobj->next = 0;
                  cobj->last = next.first;
               }
               if (this->items.compare_exchange_weak(
                  cur.bits, next.bits,
                  std::memory_order_release,
                  std::memory_order_relaxed
               )) {
                  return cur.count == 0;
               }
            }
         }
         void DumpInto(Private& receiver, ObjectRegion owner) {
            ChainEntry items = this->items.exchange(0, std::memory_order_seq_cst);
            if (items.bits) {
               auto cfirst = ObjectChain(items.first + uintptr_t(owner));
               if (receiver.first) {
                  auto rlast = Private::ObjectChain(receiver.last + uintptr_t(owner));
                  rlast->next = items.first;
               }
               else {
                  receiver.first = items.first;
               }
               receiver.last = cfirst->last;
               receiver.count += items.count;
            }
         }
      };

      struct Slab {
         size_t scan_position = 0;
         size_t objects_count = 0;
         uint32_t length() {
            return this->objects_count;
         }
      };
   };

   /**********************************************************************
   *
   *   Object Region structure
   *
   ***********************************************************************/
   struct sObjectRegion : sRegion {

      struct tListLinks {
         sObjectRegion* usables = none();
         std::atomic<sObjectRegion*> notified = none();
      };

      uint8_t layout = 0;
      const tObjectLayoutInfos& infos;
      IObjectRegionOwner* owner = 0;
      tListLinks next;

      ObjectRegionBucket::Private objects_bin;
      ObjectRegionBucket::Shared shared_bin;
      ObjectRegionBucket::Slab slab_bin;

      bool IsEmpty() {
         return this->GetAvailablesCount() == infos.object_count;
      }

      size_t GetAvailablesCount() {
         return this->objects_bin.length() + this->slab_bin.length() + this->shared_bin.length();
      }

      size_t GetSize() override {
         return size_t(1) << ObjectLayoutInfos[layout].region_sizeL2;
      }

      ObjectHeader AcquireSlabObjectAt(size_t slabIndex) {
         auto& slab = this->GetSlab(slabIndex);
         if (slab.availables) {

            // Get an object index
            auto index = lsb_32(slab.availables);

            // Prepare new object
            auto offset = this->infos.object_base + (slabIndex * cst::ObjectPerSlab + index) * this->infos.object_size;
            auto obj = ObjectHeader(&ObjectBytes(this)[offset]);
            obj->bits = sObjectHeader::cDisposedHeaderBits;

            // Publish object as ready
            auto bit = uint32_t(1) << index;
            slab.availables ^= bit;

            this->slab_bin.objects_count--;
            return obj;
         }
         else {
            return 0;
         }
      }

      ObjectHeader AcquireObject() {
         if (auto obj = objects_bin.PopObject(this)) {
            _ASSERT(obj->used == 0);
            obj->bits = sObjectHeader::cDisposedHeaderBits;
            return obj;
         }
         return this->AcquireSlabObject();
      }

      ObjectHeader AcquireSlabObject() {
         if (auto obj = this->AcquireSlabObjectAt(this->slab_bin.scan_position)) {
            return obj;
         }
         if (this->slab_bin.length()) {
            if (auto obj = this->AcquireSlabObjectSlow()) {
               return obj;
            }
            else {
               throw "Corrupted slab table";
            }
         }
         return 0;
      }

      ObjectHeader AcquireSlabObjectSlow() {
         _ASSERT(this->GetSlab(this->slab_bin.scan_position).availables == 0);
         auto initial_position = this->slab_bin.scan_position;
         while (this->slab_bin.scan_position < infos.slab_count) {
            if (auto obj = this->AcquireSlabObjectAt(this->slab_bin.scan_position)) {
               return obj;
            }
            this->slab_bin.scan_position++;
         }
         this->slab_bin.scan_position = 0;
         while (this->slab_bin.scan_position < initial_position) {
            if (auto obj = this->AcquireSlabObjectAt(this->slab_bin.scan_position)) {
               return obj;
            }
            this->slab_bin.scan_position++;
         }
         return 0;
      }

      void DisposeSlabObject(ObjectHeader obj) {
         auto mask = (uintptr_t(1) << infos.region_sizeL2) - 1;
         auto offset = uintptr_t(obj) - uintptr_t(this) - infos.object_base;
         auto index = getBlockOffsetToIndex(infos.object_divider, offset, mask);

         auto& slab = this->GetSlab(index >> cst::ObjectPerSlabL2);
         slab.availables |= uint32_t(1) << (index & cst::ObjectSlabMask);
         this->slab_bin.objects_count++;
      }

      void DisposeExchangedObject(ObjectHeader obj) {
         if (this->shared_bin.PushObject(obj, this)) {
            this->owner->NotifyAvailableRegion(this);
         }
      }

      static sObjectRegion* New(uint8_t layout);

      static constexpr sObjectRegion* none() {
         return (sObjectRegion*)-1;
      }

   protected:
      friend class sDescriptorAllocator;

      sObjectRegion(uint8_t layout)
         : layout(layout), infos(ObjectLayoutInfos[layout])
      {
         auto& infos = ObjectLayoutInfos[layout];
         auto lastIndex = infos.slab_count - 1;
         auto slabs = this->GetSlabs();
         for (int i = 0; i < lastIndex; i++) {
            auto& slab = slabs[i];
            slab.Init();
            slab.availables = 0xffffffff;
         }
         {
            auto& slab = slabs[lastIndex];
            slab.Init();
            slab.availables = (uint64_t(1) << (infos.object_count & cst::ObjectSlabMask)) - 1;
         }
         this->slab_bin.objects_count = infos.object_count;
         printf("! new region: %p: object_size=%d object_count=%d\n", this, infos.object_size, infos.object_count);
      }

      ObjectSlab GetSlabs() {
         return ObjectSlab(&ObjectBytes(this)[cst::ObjectRegionHeadSize]);
      }
      sObjectSlab& GetSlab(size_t i) {
         return ObjectSlab(&ObjectBytes(this)[cst::ObjectRegionHeadSize])[i];
      }
   };
   static_assert(sizeof(sObjectRegion) <= cst::ObjectRegionHeadSize, "bad size");

   /**********************************************************************
   *
   *   Object Region Pool
   *
   ***********************************************************************/
   struct ObjectRegionPool {
   private:

      // Usables region
      ObjectRegion usables = 0;
      ObjectRegion last_usables = 0;

      // Notified as usables region
      std::atomic<ObjectRegion> notifieds = 0;

   public:
      void PushNotifiedRegion(ObjectRegion region) {
         printf("PushNotifiedRegion\n");
         _ASSERT(region->next.notified == sObjectRegion::none());
         for (;;) {
            auto cur = this->notifieds.load(std::memory_order_relaxed);
            region->next.notified.store(cur, std::memory_order_relaxed);
            if (this->notifieds.compare_exchange_weak(
               cur, region,
               std::memory_order_release,
               std::memory_order_relaxed
            )) {
               return;
            }
         }
      }
      void PushUsableRegion(ObjectRegion region) {
         printf("PushUsableRegion\n");
         if (region->IsEmpty()) {
            printf("empty\n");
         }
         if (region->next.usables == sObjectRegion::none()) {
            region->next.usables = 0;
            if (!this->usables) this->usables = region;
            else this->last_usables->next.usables = region;
            this->last_usables = region;
         }
         else {
            printf("overpush\n");
         }
      }
      ObjectHeader AcquireObject() {
         do {

            // Try acquire in available region
            while (auto region = this->usables) {
               if (auto obj = region->AcquireObject()) {
                  return obj;
               }
               this->usables = region->next.usables;
               region->next.usables = sObjectRegion::none();
               if (region->IsEmpty()) {
                  printf("empty\n");
               }
            }
            this->last_usables = 0;

            // Collect notified regions garbage and retry
         } while (this->CollectNotifiedRegions());
         return 0;
      }
      int AcquireObjectBatch(ObjectBucket& bucket) {
         int expected_count = bucket.batch_length;
         int count = 0;
         do {

            // Try acquire in available region
            while (auto region = this->usables) {
               while (auto obj = region->AcquireObject()) {
                  bucket.PushObject(obj);
                  count++;
                  if (count >= expected_count) {
                     return count;
                  }
               }
               this->usables = region->next.usables;
               region->next.usables = sObjectRegion::none();
            }
            this->last_usables = 0;

            // Collect notified regions garbage and retry
         } while (count < expected_count && this->CollectNotifiedRegions());
         return count;
      }
      bool CollectNotifiedRegions() {
         ObjectRegion notifieds = this->notifieds.exchange(0);
         while (notifieds) {
            auto region = notifieds;
            region->shared_bin.DumpInto(region->objects_bin, region);
            if (region->objects_bin.length()) {
               this->PushUsableRegion(region);
            }
            notifieds = region->next.notified;
            region->next.notified = sObjectRegion::none();
         }
         return this->usables != 0;
      }
   };
}
