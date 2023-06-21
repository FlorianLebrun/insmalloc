#include <ins/memory/descriptors.h>
#include <ins/memory/controller.h>
#include <ins/timing.h>
#include <mutex>
#include <thread>
#include <iostream>
#include <condition_variable>

using namespace ins;
using namespace ins::mem;

ObjectAnalysisSession* mem::ObjectAnalysisSession::enabled = 0;

struct DeepMarkerContext : TraversalContext<sObjectSchema, uint8_t> {
   ObjectAnalysisSession* session;
   uint32_t depth;
   DeepMarkerContext(ObjectAnalysisSession* session, uint32_t depth) : session(session), depth(depth) {
      this->visit_ptr = fVisitPtr(VisitPtr);
   }
   void Traverse(ObjectHeader obj) {
      _ASSERT(this->depth > 0);
      if (obj->schema_id) {
         this->depth--;
         this->data = (uint8_t*)&obj[1];
         this->schema = mem::GetObjectSchema(obj->schema_id);
         this->schema->traverser((TraversalContext<>*)this);
         this->depth++;
      }
   }
   void Mark(address_t address) {
      auto entry = mem::ArenaMap[address.arenaID];
      if (entry.managed) {
         auto regionIndex = address.position >> entry.segmentation;
         auto regionLayout = entry.layout(regionIndex);
         if (regionLayout.IsObjectRegion()) {
            auto& infos = mem::cst::ObjectLayoutBase[regionLayout];
            auto offset = address.position & mem::cst::RegionMasks[entry.segmentation];
            auto objectIndex = infos.GetObjectIndex(offset);
            auto objectBit = uint64_t(1) << objectIndex;
            if (session->MarkAlive(address.arenaID, regionIndex, objectBit)) {
               if (this->depth == 0) {
                  this->session->Postpone(address.arenaID, regionIndex, objectBit);
               }
               else {
                  auto obj = &address.as<sObjectHeader>()[-1];
                  this->Traverse(obj);
               }
            }
         }
      }
   }
   static void VisitPtr(DeepMarkerContext* self, void* ptr) {
      self->Mark(ptr);
   }
};

__declspec(noinline) bool mem::ObjectAnalysisSession::MarkAlive(uint16_t arenaID, uint32_t regionIndex, uint64_t objectBit) {
   auto index = this->arenaIndexesMap[arenaID] + regionIndex;
   auto prev = this->regionAlivenessMap[index].flags.fetch_or(objectBit);
   return (prev & objectBit) == 0;
}

void mem::ObjectAnalysisSession::Postpone(uint16_t arenaID, uint32_t regionIndex, uint64_t objectBit) {
   auto index = this->arenaIndexesMap[arenaID] + regionIndex;
   auto item = &this->regionItemsMap[index];
   auto prev = item->uncheckeds.fetch_or(objectBit);
   if (prev == 0) {
      item->arenaID = arenaID;
      for (;;) {
         auto cur = this->notifieds.load(std::memory_order_relaxed);
         item->next.store(cur, std::memory_order_relaxed);
         if (this->notifieds.compare_exchange_weak(
            cur, index,
            std::memory_order_release,
            std::memory_order_relaxed
         )) {
            break;
         }
      }
   }
}

void mem::ObjectAnalysisSession::MarkPtr(void* target) {
   DeepMarkerContext(ObjectAnalysisSession::enabled, 1).Mark(target);
}

void mem::ObjectAnalysisSession::Reset() {
   uint32_t count = 1; // start at 1 because 0 is the reserved null index
   if (!this->arenaIndexesMap) {
      this->arenaIndexesMap = (uint32_t*)malloc(sizeof(uint32_t) * cst::ArenaPerSpace);
   }
   for (int i = 0; i < cst::ArenaPerSpace; i++) {
      auto& entry = mem::ArenaMap[i];
      this->arenaIndexesMap[i] = count;
      if (entry.managed) {
         auto arena = entry.descriptor();
         count += arena->availables_count.load();
      }
   }
   if (count > this->allocated) {
      if (this->regionAlivenessMap) {
         free(this->regionAlivenessMap);
         free(this->regionItemsMap);
      }
      this->regionAlivenessMap = (ObjectAlivenessFlags*)malloc(sizeof(ObjectAlivenessFlags) * count);
      this->regionItemsMap = (ObjectAlivenessItem*)malloc(sizeof(ObjectAlivenessItem) * count);
      this->allocated = count;
   }
   memset(this->regionAlivenessMap, 0, sizeof(ObjectAlivenessFlags) * count);
   memset(this->regionItemsMap, 0, sizeof(ObjectAlivenessItem) * count);
   this->length = count;
}

void mem::ObjectAnalysisSession::RunOnce() {
   while (auto workIndex = this->notifieds.exchange(0)) {
      uint32_t nextIndex = 0;
      do {
         // Pop item to analyze
         auto& item = this->regionItemsMap[workIndex];
         nextIndex = item.next.load();
         item.next = 0;

         // Treat uncheckeds objects
         auto entry = mem::ArenaMap[item.arenaID];
         auto regionIndex = workIndex - this->arenaIndexesMap[item.arenaID];
         auto regionLayout = cst::ObjectLayoutBase[entry.layout(regionIndex)];
         auto regionBase = (uintptr_t(item.arenaID) << cst::ArenaSizeL2) + (uintptr_t(regionIndex) << entry.segmentation) + cst::ObjectRegionHeadSize;
         auto workBits = item.uncheckeds.exchange(0);
         while (workBits) {
            auto objectIndex = bit::lsb_64(workBits);
            auto object = ObjectHeader(regionBase + objectIndex * regionLayout.object_multiplier);
            workBits ^= uint64_t(1) << objectIndex;
            DeepMarkerContext(ObjectAnalysisSession::enabled, 5).Traverse(object);
         }

      } while (workIndex = nextIndex);
   }
}

void mem::ObjectMemoryCollection::iterator::move() {
   // Move to next region when end reach
   while (this->regionObjects == 0) {
      this->regionIndex++;

      // Move to next arena when end reach
      while (this->regionIndex >= this->regionCount) {

         this->arenaIndex++;
         if (this->arenaIndex >= cst::ArenaPerSpace) return this->complete();
         this->arena = mem::ArenaMap[this->arenaIndex];

         auto arena = this->arena.descriptor();
         auto region_size = size_t(1) << arena->segmentation;
         this->regionCount = arena->availables_count.load(std::memory_order_relaxed);
         this->regionIndex = 0;
      }

      auto& regionEntry = this->arena->regions[this->regionIndex];
      if (regionEntry.IsObjectRegion()) {
         this->region = ObjectRegion((this->arenaIndex << cst::ArenaSizeL2) + (uintptr_t(this->regionIndex) << this->arena.segmentation));
         this->layout = this->region->layoutID;
         this->regionObjects = this->region->GetUsedMap();
      }
   }

   this->index = bit::lsb_64(this->regionObjects);
   this->object = this->region->GetObjectAt(this->index);
   this->regionObjects ^= uint64_t(1) << this->index;
}