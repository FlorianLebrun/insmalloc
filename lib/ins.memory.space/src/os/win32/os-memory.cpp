#include <ins/binary/alignment.h>
#include <ins/memory/os-memory.h>

#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#include <stdio.h>
#include <winternl.h>
#include <Dbghelp.h>
#include <assert.h>
#pragma comment(lib,"Dbghelp.lib")
#pragma comment(lib,"Ntdll.lib")

namespace ins {
   namespace OSMemory {

      uintptr_t GetMemorySize() {
         SYSTEM_INFO infos;
         GetSystemInfo(&infos);
         return (uintptr_t)infos.lpMaximumApplicationAddress;
      }

      uintptr_t GetSlabSize() {
         SYSTEM_INFO infos;
         GetSystemInfo(&infos);
         if (infos.dwPageSize > infos.dwAllocationGranularity) return infos.dwPageSize;
         else return infos.dwAllocationGranularity;
      }

      tZoneState GetMemoryZoneState(uintptr_t address) {
         MEMORY_BASIC_INFORMATION infos;
         tZoneState region;
         if (!VirtualQuery(LPVOID(address), &infos, sizeof(MEMORY_BASIC_INFORMATION))) {
            region.address = address;
            region.size = 0;
            region.state = tState::OUT_OF_MEMORY;
         }
         else {
            region.address = (uintptr_t)infos.BaseAddress;
            region.size = infos.RegionSize;
            switch (infos.State) {
            case MEM_COMMIT:region.state = tState::COMMITTED; break;
            case MEM_RESERVE:region.state = tState::RESERVED; break;
            case MEM_FREE:region.state = tState::FREE; break;
            }
         }
         return region;
      }

      void EnumerateMemoryZone(uintptr_t startAddress, uintptr_t endAddress, std::function<void(tZoneState&)> visitor) {
         uintptr_t cursor = startAddress;
         while (cursor < endAddress) {
            tZoneState zone = OSMemory::GetMemoryZoneState(cursor);
            cursor = zone.address + zone.size;
            visitor(zone);
         }
      }

      uintptr_t AcquireMemory(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement, DWORD flAllocationType, DWORD flProtect) {
         if (!limit) {
            limit = size_t(1) << 48;
         }
         while (base < limit) {

            // Allocate from aligned base
            uintptr_t ptr = (uintptr_t)VirtualAlloc(LPVOID(base), size, flAllocationType, flProtect);
            if (ptr >= base) {
               if (ptr % alignement == 0 && ptr < limit) {
                  return ptr;
               }
               else if (ptr > base) {
                  base = alignX<intptr_t>(ptr, alignement);
               }
               VirtualFree(LPVOID(ptr), size, MEM_RELEASE);
               printf("! unsuseful reservation detected !\n");
            }

            // Try again for aligned base after found ptr
            base += alignement;
         }
         return 0;
      }

      uintptr_t AllocBuffer(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement) {
         return AcquireMemory(base, limit, size, alignement, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
      }

      uintptr_t ReserveMemory(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement) {
         return AcquireMemory(base, limit, size, alignement, MEM_RESERVE, PAGE_NOACCESS);
      }

      bool CommitMemory(uintptr_t base, uintptr_t size) {
         uintptr_t ptr = (uintptr_t)VirtualAlloc(LPVOID(base), size, MEM_COMMIT, PAGE_READWRITE);
         assert(base == ptr);
         return base == ptr;
      }

      bool DecommitMemory(uintptr_t base, uintptr_t size) {
         return VirtualFree(LPVOID(base), size, MEM_DECOMMIT);
      }

      bool ReleaseMemory(uintptr_t base, uintptr_t size) {
         return VirtualFree(LPVOID(base), 0, MEM_RELEASE);
      }

   }
}