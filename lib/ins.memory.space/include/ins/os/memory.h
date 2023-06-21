#pragma once
#include <functional>
#include <stdint.h>

namespace ins::os {

   enum tState {
      FREE,
      RESERVED,
      COMMITTED,
      OUT_OF_MEMORY,
   };

   struct tZoneState {
      tState state;
      uintptr_t address;
      uintptr_t size;
   };

   struct tProcessWorkingSet {
      size_t total_bytes = 0;
      size_t private_bytes = 0;
      size_t shared_bytes = 0;
   };

   uintptr_t GetMemorySize();
   uintptr_t GetSlabSize();

   tProcessWorkingSet GetMemoryWorkingSet();
   
   tZoneState GetMemoryZoneState(uintptr_t address);
   void EnumerateMemoryZone(uintptr_t startAddress, uintptr_t endAddress, std::function<void(tZoneState&)> visitor);

   uintptr_t AllocateMemory(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement);
   uintptr_t ReserveMemory(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement);

   bool CommitMemory(uintptr_t base, uintptr_t size);
   bool DecommitMemory(uintptr_t base, uintptr_t size);
   bool ReleaseMemory(uintptr_t base, uintptr_t size);
}
