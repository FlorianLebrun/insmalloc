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
         bool IsEmpty() {
            return this->count == 0;
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
         bool IsEmpty() {
            return this->items.load(std::memory_order_relaxed) == 0;
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
         sObjectRegion* used = none();
         std::atomic<sObjectRegion*> notified = none();
      };

      uint8_t layout = 0;
      const tObjectLayoutInfos& infos;
      IObjectRegionOwner* owner = 0;
      tListLinks next;

      ObjectRegionBucket::Private objects_bin;
      ObjectRegionBucket::Shared shared_bin;
      ObjectRegionBucket::Slab slab_bin;

      bool IsDisposable() {
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
         if (auto obj = this->objects_bin.PopObject(this)) {
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

      void DisposeObject(ObjectHeader obj) {
         this->objects_bin.PushObject(obj, this);
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
      friend class sDescriptorHeap;

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
         _INS_DEBUG(printf("! new region: %p: object_size=%d object_count=%d\n", this, infos.object_size, infos.object_count));
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
   *   Object Region List
   *
   ***********************************************************************/
   struct ObjectRegionList {
      ObjectRegion first = 0;
      ObjectRegion last = 0;
      uint32_t count = 0;

      void PushRegion(ObjectRegion region) {
         region->next.used = 0;
         if (!this->last) this->first = region;
         else this->last->next.used = region;
         this->last = region;
         this->count++;
      }
      ObjectRegion PopRegion() {
         if (auto region = this->first) {
            this->first = region->next.used;
            if (!this->first) this->last = 0;
            this->count--;
            region->next.used = sObjectRegion::none();
            return region;
         }
         return 0;
      }
      void DumpInto(ObjectRegionList& receiver, IObjectRegionOwner* owner) {
         if (this->first) {
            for (ObjectRegion region = this->first; region; region = region->next.used) {
               region->owner = owner;
            }
            if (receiver.last) {
               receiver.last->next.used = this->first;
               receiver.last = this->last;
               receiver.count += this->count;
            }
            else {
               receiver.first = this->first;
               receiver.last = this->last;
               receiver.count = this->count;
            }
            this->first = 0;
            this->last = 0;
            this->count = 0;
         }
      }
      void CheckValidity() {
         uint32_t c_count = 0;
         for (auto x = this->first; x && c_count < 1000000; x = x->next.used) {
            if (!x->next.used) _ASSERT(x == this->last);
            c_count++;
         }
         _ASSERT(c_count == this->count);
      }
   };

   /**********************************************************************
   *
   *   Object Region Pool
   *
   ***********************************************************************/
   struct ObjectRegionPool {
   public:
      // Owned regions counter
      uint32_t owneds_count = 0;

      // Used regions
      ObjectRegionList usables;
      ObjectRegionList disposables; // Empties region

      // Notified as usables region
      std::atomic<ObjectRegion> notifieds = 0;

      void AddNewRegion(ObjectRegion region) {
         _INS_DEBUG(printf("AddNewRegion\n"));
         this->usables.PushRegion(region);
         this->owneds_count++;
      }

      void PushDisposableRegion(ObjectRegion region) {
         _INS_DEBUG(printf("PushDisposableRegion\n"));
         _ASSERT(region->IsDisposable());
         this->disposables.PushRegion(region);
      }

      void PushUsableRegion(ObjectRegion region) {
         if (region->next.used == sObjectRegion::none()) {
            if (usables.count > 1 && region->IsDisposable()) {
               this->PushDisposableRegion(region);
            }
            else {
               _INS_DEBUG(printf("PushUsableRegion\n"));
               this->usables.PushRegion(region);
            }
         }
         else {
            _INS_DEBUG(printf("overpush\n"));
         }
      }

      void PushNotifiedRegion(ObjectRegion region) {
         _INS_DEBUG(printf("PushNotifiedRegion\n"));
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

      ObjectHeader AcquireObject() {
         do {
            do {
               if (auto used = this->usables.first) {
                  if (auto obj = used->AcquireObject()) {
                     return obj;
                  }
               }
            } while (this->PullNextUsedRegion());
         } while (this->ScavengeNotifiedRegions());
         return 0;
      }

      uint32_t AcquireObjectBatch(ObjectBucket& bucket) {
         uint32_t expected_count = bucket.batch_length;
         uint32_t count = 0;
         do {
            do {
               if (auto used = this->usables.first) {
                  while (auto obj = used->AcquireObject()) {
                     bucket.PushObject(obj);
                     count++;
                     if (count >= expected_count) {
                        return count;
                     }
                  }
               }
            } while (this->PullNextUsedRegion());
         } while (this->ScavengeNotifiedRegions());
         return count;
      }

      void DumpInto(ObjectRegionPool& receiver, IObjectRegionOwner* owner) {
         this->ScavengeNotifiedRegions();
         uint32_t transfered_count = this->usables.count + this->disposables.count;
         receiver.owneds_count += transfered_count;
         this->owneds_count -= transfered_count;
         this->usables.DumpInto(receiver.usables, owner);
         this->disposables.DumpInto(receiver.disposables, owner);
      }

      void CleanDisposables() {
         this->ScavengeNotifiedRegions();

         // Purge disposables from usables list
         ObjectRegion* pregion = &this->usables.first;
         ObjectRegion last = 0;
         while (*pregion) {
            auto region = *pregion;
            if (region->IsDisposable()) {
               *pregion = region->next.used;
               this->usables.count--;
               this->disposables.PushRegion(region);
            }
            else {
               last = region;
               pregion = &region->next.used;
            }
         }
         this->usables.last = last;
      }

      void CheckValidity();
   private:
      bool PullNextUsedRegion() {
         auto prevUsed = this->usables.PopRegion();
         _ASSERT(!prevUsed || (prevUsed->objects_bin.length() + prevUsed->slab_bin.length()) == 0);
         if (this->usables.first) {
            while (this->usables.count > 1 && this->usables.first->IsDisposable()) {
               auto disposable = this->usables.PopRegion();
               this->PushDisposableRegion(disposable);
            }
            return true;
         }
         else if (auto usable = this->disposables.PopRegion()) {
            this->usables.PushRegion(usable);
            return true;
         }
         return false;
      }
      bool ScavengeNotifiedRegions() {
         uint32_t collecteds = 0;
         ObjectRegion region = this->notifieds.exchange(0);
         while (region) {
            auto next_region = region->next.notified.load();
            region->next.notified = sObjectRegion::none();
            if (!region->shared_bin.IsEmpty()) {
               region->shared_bin.DumpInto(region->objects_bin, region);
            }
            if (!region->objects_bin.IsEmpty()) {
               this->PushUsableRegion(region);
               collecteds++;
            }
            region = next_region;
         }
         return collecteds > 0;
      }
   };
}
