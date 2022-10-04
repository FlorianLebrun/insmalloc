#pragma once
#include <ins/memory/structs.h>
#include <ins/binary/alignment.h>
#include <ins/binary/bitwise.h>
#include <ins/memory/objects.h>
#include <ins/memory/objects-region.h>
#include <ins/memory/objects-context.h>

namespace ins {

   namespace MemorySpace {

      struct ArenaBucket {
         sArenaDescriptor* availables = 0;
      };

      struct tMemoryState {
         std::mutex lock;
         sDescriptorHeap* descriptorHeap = 0;
         ArenaBucket arenas[cst::ArenaSizeL2]; // Arenas with free regions
         ArenaEntry table[cst::ArenaPerSpace];

         tMemoryState();
         void RunController();
         void ScheduleHeapMaintenance(ins::ObjectClassHeap* heap);
      };
      extern tMemoryState state;

      // Arena API
      address_t ReserveArena();

      // Region API
      void SetRegionEntry(address_t address, RegionEntry entry);
      RegionEntry GetRegionEntry(address_t address);
      Descriptor GetRegionDescriptor(address_t address);
      void ForeachRegion(std::function<bool(ArenaDescriptor arena, RegionEntry entry, address_t addr)>&& visitor);

      address_t AllocateRegion(uint32_t sizing);
      void DisposeRegion(address_t address);

      // Objects API
      ObjectHeader GetObject(address_t ptr);
      void ForeachObjectRegion(std::function<bool(ObjectRegion)>&& visitor);

      // Utils API
      void Print();
   }
}