#include <stdint.h>
#include <semaphore>
#include <stdlib.h>
#include <ins/memory/objects.h>
#include <ins/memory/space.h>

using namespace ins;

sObjectRegion* sObjectRegion::New(uint8_t layout) {
   auto& infos = ObjectLayoutInfos[layout];
   auto region = new(space.AllocateRegion(infos.region_sizeL2)) sObjectRegion(layout);
   space.SetRegionEntry(region, RegionEntry::ObjectRegion(layout));
   return region;
}

ObjectHeader ObjectBucket::PopObject() {
   if (auto obj = this->items) {
      if (obj->nextObject) {
         this->items = obj->nextObject;
      }
      else {
         this->items = ObjectChain(obj->nextList);
         this->list_count--;
      }
      this->count--;
      obj->used = 1;
      return obj;
   }
   return 0;
}

void ObjectBucket::PushObject(ObjectHeader obj) {
   ObjectChain freelist = 0;
   auto cobj = ObjectChain(obj);
   obj->used = 0;
   if (this->items && this->items->length < this->list_length) {
      cobj->nextObject = this->items;
      cobj->nextList = 0;
      cobj->length = this->items->length + 1;
   }
   else {
      cobj->nextObject = 0;
      cobj->nextList = uint64_t(this->items);
      cobj->length = 0;
      this->list_count++;
   }
   this->items = cobj;
   this->count++;
   //printf("%p: %d/%d\n", this, this->count, this->list_count);
}

bool ObjectBucket::TransfertBatch(ObjectBucket& receiver) {
   if (ObjectChain batch = this->items) {

      // Pull one batch from this provider
      this->items = ObjectChain(batch->nextList);
      this->count -= batch->length;
      this->list_count--;

      // Push one batch to receiver
      if (receiver.items) {
         batch->nextList = receiver.items->nextList;
         receiver.items->nextList = uint64_t(batch);
      }
      else {
         batch->nextList = 0;
         receiver.items = batch;
      }
      receiver.count += batch->length;
      receiver.list_count++;

      return true;
   }
   return false;
}
