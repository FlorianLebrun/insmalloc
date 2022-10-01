#pragma once
#include <ins/memory/structs.h>
#include <ins/binary/alignment.h>
#include <ins/binary/bitwise.h>
#include <ins/memory/objects.h>
#include <ins/memory/objects-region.h>

namespace ins {

   struct MemoryHeap {
   };

   struct MemoryContext {
      MemoryHeap* heap;
      //ObjectClassContext classes[100];

      //ObjectHeader AllocateObject(int id);
      //void DisposeObject(void* ptr);
   };

   struct MemorySpace {
      struct RegionBucket {
         sArenaDescriptor* availables;
      };

      sDescriptorAllocator* descriptorAllocator = 0;

      std::mutex lock;
      RegionBucket regions[cst::ArenaSizeL2]; // Arenas with free regions
      ArenaEntry arenas[cst::ArenaPerSpace];

      MemorySpace();
      void SetRegionEntry(address_t address, RegionEntry entry);
      RegionEntry GetRegionEntry(address_t address);
      Descriptor GetRegionDescriptor(address_t address);
      address_t ReserveArena();

      address_t AllocateRegion(uint32_t sizing);
      void DisposeRegion(address_t base, uint32_t sizing);

      void RunController();
      void ScheduleHeapMaintenance(ins::ObjectClassHeap* heap);

      ObjectHeader GetObject(address_t ptr);

      void Print();

   private:
      sArenaDescriptor* AcquireArena_unsafe(uint32_t segmentation);
   };

   extern MemorySpace space;
}
