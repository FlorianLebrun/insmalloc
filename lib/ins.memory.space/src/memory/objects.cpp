#include <stdint.h>
#include <semaphore>
#include <stdlib.h>
#include <ins/memory/objects.h>
#include <ins/memory/space.h>

using namespace ins;

sSlabbedObjectRegion* sSlabbedObjectRegion::New(uint8_t layout) {
   auto& infos = ObjectLayoutInfos[layout];
   auto region = new(MemorySpace::AllocateRegion(infos.region_sizeL2)) sSlabbedObjectRegion(layout);
   MemorySpace::SetRegionEntry(region, RegionEntry::ObjectRegion(layout));
   return region;
}

ObjectHeader ObjectBucket::PopObject() {
   if (auto obj = this->items) {
      this->items = obj->nextObject;
      this->count--;
      return obj;
   }
   else if (auto batch = this->batches) {
      this->items = batch;
      this->batches = ObjectChain(batch->nextBatch);
      batch->nextBatch = 0;
      this->batch_count--;
      return this->PopObject();
   }
   return 0;
}

void ObjectBucket::PushObject(ObjectHeader obj) {
   auto cobj = ObjectChain(obj);
   _ASSERT(obj->used == 0);

   // Flush items to a new batch
   auto prev_items = this->items;
   if (prev_items && prev_items->length >= this->batch_length) {
      prev_items->nextBatch = uint64_t(this->batches);
      this->batches = prev_items;
      this->batch_count++;
      prev_items = 0;
   }

   // Add object to items
   if (!prev_items) {
      cobj->nextObject = 0;
      cobj->length = 1;
      this->items = cobj;
      this->count++;
   }
   else {
      cobj->nextObject = prev_items;
      cobj->length = prev_items->length + 1;
      this->items = cobj;
      this->count++;
   }
}

bool ObjectBucket::TransfertBatchInto(ObjectBucket& receiver) {
   if (ObjectChain batch = this->batches) {

      // Pull one batch from this provider
      this->batches = ObjectChain(batch->nextBatch);
      this->count -= batch->length;
      this->batch_count--;

      // Push one batch to receiver
      if (receiver.batches) {
         batch->nextBatch = receiver.batches->nextBatch;
         receiver.batches->nextBatch = uint64_t(batch);
      }
      else {
         batch->nextBatch = 0;
         receiver.batches = batch;
      }
      receiver.count += batch->length;
      receiver.batch_count++;

      return true;
   }
   return false;
}

void ObjectBucket::DumpInto(ObjectBucket& receiver) {
   if (ObjectChain batch = this->batches) {
      ObjectChain last_batch = batch;
      while (last_batch->nextBatch) last_batch = ObjectChain(last_batch->nextBatch);
      last_batch->nextBatch = uint64_t(receiver.batches);
      receiver.batches = batch;
      receiver.batch_count += this->batch_count;
      this->batch_count = 0;
      this->batches = 0;
   }
   if (ObjectChain items = this->items) {
      items->nextBatch = uint64_t(receiver.batches);
      receiver.batches = items;
      receiver.batch_count++;
      this->items = 0;
   }
   receiver.count += this->count;
   this->count = 0;
}

void ObjectBucket::CheckValidity() {
   int obj_count_1 = 0;
   int obj_count_2 = 0;
   int batch_count = 0;
   if (this->items) {
      for (auto obj = this->items; obj; obj = obj->nextObject) {
         obj_count_1++;
      }
      obj_count_2 += this->items->length;
   }
   for (auto batch = this->batches; batch; batch = ObjectChain(batch->nextBatch)) {
      for (auto obj = batch; obj; obj = obj->nextObject) {
         obj_count_1++;
      }
      obj_count_2 += batch->length;
      batch_count++;
   }
   _ASSERT(obj_count_1 == this->count);
   _ASSERT(obj_count_2 == this->count);
   _ASSERT(batch_count == this->batch_count);
}
