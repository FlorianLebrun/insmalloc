#pragma once
#include <functional>
#include <stdint.h>

namespace ins {
   namespace OSMemory {
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

      uintptr_t GetMemorySize();
      uintptr_t GetSlabSize();
      tZoneState GetMemoryZoneState(uintptr_t address);
      void EnumerateMemoryZone(uintptr_t startAddress, uintptr_t endAddress, std::function<void(tZoneState&)> visitor);


      uintptr_t AllocBuffer(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement);

      uintptr_t ReserveMemory(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement);
      bool CommitMemory(uintptr_t base, uintptr_t size);
      bool DecommitMemory(uintptr_t base, uintptr_t size);
      bool ReleaseMemory(uintptr_t base, uintptr_t size);
   }
}
