#include "../../../common/alignment.h"
#include "./system.h"

#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#include <stdio.h>
#include <winternl.h>
#include <Dbghelp.h>
#include <assert.h>
#pragma comment(lib,"Dbghelp.lib")
#pragma comment(lib,"Ntdll.lib")

void InitSystemFeatures() {
   // Nothing to do for now
}

uintptr_t SystemMemory::GetMemorySize() {
   SYSTEM_INFO infos;
   GetSystemInfo(&infos);
   return (uintptr_t)infos.lpMaximumApplicationAddress;
}

uintptr_t SystemMemory::GetPageSize() {
   SYSTEM_INFO infos;
   GetSystemInfo(&infos);
   if (infos.dwPageSize > infos.dwAllocationGranularity) return infos.dwPageSize;
   else return infos.dwAllocationGranularity;
}

SystemMemory::tZoneState SystemMemory::GetMemoryZoneState(uintptr_t address) {
   MEMORY_BASIC_INFORMATION infos;
   SystemMemory::tZoneState region;
   if (!VirtualQuery(LPVOID(address), &infos, sizeof(MEMORY_BASIC_INFORMATION))) {
      region.address = address;
      region.size = 0;
      region.state = SystemMemory::tState::OUT_OF_MEMORY;
   }
   else {
      region.address = (uintptr_t)infos.BaseAddress;
      region.size = infos.RegionSize;
      switch (infos.State) {
      case MEM_COMMIT:region.state = SystemMemory::tState::COMMITTED; break;
      case MEM_RESERVE:region.state = SystemMemory::tState::RESERVED; break;
      case MEM_FREE:region.state = SystemMemory::tState::FREE; break;
      }
   }
   return region;
}

uintptr_t SystemMemory::AllocBuffer(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement) {
   while (base < (uintptr_t)limit) {

      // Allocate from aligned base
      uintptr_t ptr = (uintptr_t)VirtualAlloc(LPVOID(base), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
      if (ptr % alignement == 0 && ptr < limit) return ptr;

      // Try again for aligned base after found ptr
      base = alignX(ptr + alignement, alignement);
      VirtualFree(LPVOID(ptr), size, MEM_RELEASE);
      printf("! unsuseful reservation detected !\n");
   }
   return 0;
}

bool SystemMemory::ReserveMemory(uintptr_t base, uintptr_t size) {
   uintptr_t ptr = (uintptr_t)VirtualAlloc(LPVOID(base), size, MEM_RESERVE, PAGE_NOACCESS);
   if (base != ptr) {
      VirtualFree(LPVOID(ptr), size, MEM_RELEASE);
      printf("! unsuseful reservation detected !\n");
      return false;
   }
   return true;
}

bool SystemMemory::CommitMemory(uintptr_t base, uintptr_t size) {
   uintptr_t ptr = (uintptr_t)VirtualAlloc(LPVOID(base), size, MEM_COMMIT, PAGE_READWRITE);
   assert(base == ptr);
   return base == ptr;
}

bool SystemMemory::DecommitMemory(uintptr_t base, uintptr_t size) {
   return VirtualFree(LPVOID(base), size, MEM_DECOMMIT);
}

bool SystemMemory::ReleaseMemory(uintptr_t base, uintptr_t size) {
   return VirtualFree(LPVOID(base), size, MEM_RELEASE);
}

