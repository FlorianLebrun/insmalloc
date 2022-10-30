#include <ins/memory/objects-refs.h>
#include <ins/memory/space.h>

using namespace ins;


struct DeepMarkerContext : TraversalContext<SchemaDesc, uint8_t> {
   ObjectAnalysisSession* session;
   uint32_t depth;
   DeepMarkerContext(ObjectAnalysisSession* session, uint32_t depth) : session(session), depth(depth) {
      this->visit_ptr = fVisitPtr(VisitPtr);
   }
   void Traverse(ObjectHeader obj) {
      _ASSERT(this->depth > 0);
      if (obj->schemaID) {
         this->depth--;
         this->data = (uint8_t*)&obj[1];
         this->schema = ins::schemasHeap.GetSchemaDesc(obj->schemaID);
         this->schema->traverser((TraversalContext<>*)this);
         this->depth++;
      }
   }
   void Mark(address_t address) {
      auto entry = ins::RegionsHeap.arenas[address.arenaID];
      if (entry.managed) {
         auto regionIndex = address.position >> entry.segmentation;
         auto regionLayout = entry.layout(regionIndex);
         if (regionLayout.IsObjectRegion()) {
            auto& infos = ins::cst::ObjectLayoutBase[regionLayout];
            auto offset = address.position & ins::cst::RegionMasks[entry.segmentation];
            auto objectIndex = infos.GetObjectIndex(offset);
            auto objectBit = uint64_t(1) << objectIndex;
            if (session->SetAlive(address.arenaID, regionIndex, objectBit)) {
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

__forceinline bool ObjectAnalysisSession::SetAlive(uint16_t arenaID, uint32_t regionIndex, uint64_t objectBit) {
   auto index = this->arenaIndexesMap[arenaID] + regionIndex;
   auto prev = this->regionAlivenessMap[index].flags.fetch_or(objectBit);
   return (prev & objectBit) == 0;
}

void ObjectAnalysisSession::Postpone(uint16_t arenaID, uint32_t regionIndex, uint64_t objectBit) {
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

void ObjectAnalysisSession::MarkPtr(void* target) {
   DeepMarkerContext(ObjectAnalysisSession::enabled, 1).Mark(target);
}

void ObjectAnalysisSession::Reset() {
   uint32_t count = 1; // start at 1 because 0 is the reserved null index
   if (!this->arenaIndexesMap) {
      this->arenaIndexesMap = (uint32_t*)malloc(sizeof(uint32_t) * cst::ArenaPerSpace);
   }
   for (int i = 0; i < cst::ArenaPerSpace; i++) {
      auto& entry = ins::RegionsHeap.arenas[i];
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

void ObjectAnalysisSession::RunOnce() {
   while (auto workIndex = this->notifieds.exchange(0)) {
      uint32_t nextIndex = 0;
      do {
         // Pop item to analyze
         auto& item = this->regionItemsMap[workIndex];
         nextIndex = item.next.load();
         item.next = 0;

         // Treat uncheckeds objects
         auto entry = ins::RegionsHeap.arenas[item.arenaID];
         auto regionIndex = workIndex - this->arenaIndexesMap[item.arenaID];
         auto regionLayout = cst::ObjectLayoutBase[entry.layout(regionIndex)];
         auto regionBase = (uintptr_t(item.arenaID) << cst::ArenaSizeL2) + (uintptr_t(regionIndex) << entry.segmentation) + cst::ObjectRegionHeadSize;
         auto workBits = item.uncheckeds.exchange(0);
         while (workBits) {
            auto objectIndex = lsb_64(workBits);
            auto object = ObjectHeader(regionBase + objectIndex * regionLayout.object_multiplier);
            workBits ^= uint64_t(1) << objectIndex;
            DeepMarkerContext(ObjectAnalysisSession::enabled, 5).Traverse(object);
         }

      } while (workIndex = nextIndex);
   }
}
