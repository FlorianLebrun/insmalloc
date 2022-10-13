#pragma once
#include <ins/memory/structs.h>
#include <ins/binary/alignment.h>
#include <ins/binary/bitwise.h>
#include <ins/memory/objects.h>
#include <ins/memory/objects-slabbed.h>
#include <ins/memory/objects-large.h>

namespace ins {
   struct SchemasHeap;

   namespace MemorySpace {

      struct ArenaBucket {
         ArenaDescriptor* availables = 0;
      };

      struct tMemoryState {
         std::mutex lock;
         ArenaBucket arenas[cst::ArenaSizeL2]; // Arenas with free regions
         ArenaEntry table[cst::ArenaPerSpace];

         tMemoryState();
         void RunController();
         void ScheduleHeapMaintenance(ins::SlabbedObjectHeap* heap);
      };
      extern tMemoryState state;

      // Arena API
      address_t ReserveArena();

      // Region API
      void SetRegionEntry(address_t address, RegionEntry entry);
      RegionEntry GetRegionEntry(address_t address);
      Descriptor* GetRegionDescriptor(address_t address);
      size_t GetRegionSize(address_t address);
      void ForeachRegion(std::function<bool(ArenaDescriptor* arena, RegionEntry entry, address_t addr)>&& visitor);

      address_t AllocateRegion(uint32_t sizing);
      void DisposeRegion(address_t address);

      // Objects API
      ObjectHeader GetObject(address_t ptr);
      void ForeachObjectRegion(std::function<bool(ObjectRegion)>&& visitor);

      // Utils API
      void Print();
   }

   typedef struct ObjectAnalyticsInfos {
      uint64_t stackstamp;
      uint64_t timestamp;
      ObjectAnalyticsInfos(uint64_t timestamp = 0, uint64_t stackstamp = 0) {
         this->stackstamp = stackstamp;
         this->timestamp = timestamp;
      }
   };
   static_assert(sizeof(ObjectAnalyticsInfos) == sizeof(uint64_t) * 2, "bad size");

   struct ObjectLocation {
      ObjectRegion region;
      ObjectHeader object;
      RegionEntry entry;
      ObjectLocation(address_t address) {
         auto arena = MemorySpace::state.table[address.arenaID];
         this->entry = ((ArenaDescriptor*)arena.reference)->regions[address.position >> arena.segmentation];
         if (this->entry.IsObjectRegion()) {
            auto& infos = ins::ObjectLayoutBase[this->entry.objectLayoutID];
            auto offset = address.position & infos.region_mask;
            auto index = infos.GetObjectIndex(offset);
            this->region = ObjectRegion(address.ptr - offset);
            this->object = ObjectHeader(uintptr_t(region) + infos.GetObjectOffset(index));
         }
         else {
            this->region = 0;
            this->object = 0;
         }
      }
   };

   struct ObjectInfos : ObjectLocation {
      ObjectInfos(address_t address) 
         : ObjectLocation(address) {
      }
      size_t allocated_size() {
         if (this->object) {
            return this->region->infos.object_size;
         }
         return 0;
      }
      size_t usable_size() {
         if (this->object) {
            auto size = this->region->infos.object_size;
            if (this->object->hasAnalyticsInfos) {
               size -= sizeof(ObjectAnalyticsInfos);
            }
            if (this->object->hasSecurityPadding) {
               auto paddingEnd = size - sizeof(uint32_t);
               auto pBufferSize = (uint32_t*)&ObjectBytes(object)[paddingEnd];
               uint32_t bufferSize = (*pBufferSize) ^ 0xabababab;
               if (bufferSize < size) size = bufferSize;
            }
            return size;
         }
         return 0;
      }
      ObjectAnalyticsInfos* getAnalyticsInfos() {
         if (this->object && this->object->hasAnalyticsInfos) {
            auto size = this->region->infos.object_size;
            auto pinfos = &ObjectBytes(object)[size - sizeof(ObjectAnalyticsInfos)];
            return (ObjectAnalyticsInfos*)pinfos;
         }
         return 0;
      }
      void* detectOverflowedBytes() {
         if (this->object && this->object->hasSecurityPadding) {
            auto size = this->region->infos.object_size;
            if (this->object->hasAnalyticsInfos) {
               size -= sizeof(ObjectAnalyticsInfos);
            }

            // Read and check padding size
            auto paddingEnd = size - sizeof(uint32_t);
            auto pBufferSize = (uint32_t*)&ObjectBytes(object)[paddingEnd];
            uint32_t bufferSize = (*pBufferSize) ^ 0xabababab;
            if (bufferSize > size) {
               return pBufferSize;
            }

            // Read and check padding bytes
            auto bytes = ObjectBytes(object);
            for (uint32_t i = bufferSize; i < paddingEnd; i++) {
               if (bytes[i] != 0xab) {
                  return &bytes[i];
               }
            }
         }
         return 0;
      }
   };
}