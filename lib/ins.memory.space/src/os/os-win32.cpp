#include <ins/binary/alignment.h>
#include <ins/os/memory.h>
#include <ins/os/threading.h>
#include <atomic>

#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#include <stdio.h>
#include <winternl.h>
#include <Dbghelp.h>
#include <assert.h>
#pragma comment(lib,"Dbghelp.lib")
#pragma comment(lib,"Ntdll.lib")

namespace ins::os {

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

   tProcessWorkingSet GetMemoryWorkingSet() {
      tProcessWorkingSet stats;
      DWORD processID = GetCurrentProcessId(); // ID du processus courant
      PSAPI_WORKING_SET_INFORMATION* pWSI = NULL;
      DWORD size = 0;

      HANDLE processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);
      if (processHandle == NULL) {
         throw;
      }
      QueryWorkingSet(processHandle, pWSI, size);

      size = sizeof(PSAPI_WORKING_SET_INFORMATION) * 1024 * 128;
      pWSI = (PSAPI_WORKING_SET_INFORMATION*)malloc(size);
      if (pWSI == NULL) {
         CloseHandle(processHandle);
         throw;
      }

      if (!QueryWorkingSet(processHandle, pWSI, size)) {
         free(pWSI);
         CloseHandle(processHandle);
         throw;
      }

      SYSTEM_INFO si;
      GetSystemInfo(&si);
      stats.shared_bytes = 0;
      stats.private_bytes = 0;
      for (DWORD i = 0; i < pWSI->NumberOfEntries; i++) {
         if (pWSI->WorkingSetInfo[i].Shared) {
            stats.shared_bytes += si.dwPageSize;
         }
         else {
            stats.private_bytes += si.dwPageSize;
         }
      }

      PROCESS_MEMORY_COUNTERS_EX pmc;
      if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
         stats.total_bytes = pmc.WorkingSetSize;
      }

      free(pWSI);
      CloseHandle(processHandle);
      return stats;
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
         tZoneState zone = os::GetMemoryZoneState(cursor);
         cursor = zone.address + zone.size;
         visitor(zone);
      }
   }

   uintptr_t AcquireMemory(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement, DWORD flAllocationType, DWORD flProtect) {
      static constexpr uint32_t unsuseful_reservation_limit = 30;
      static std::atomic_uint32_t unsuseful_reservation;
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
               base = bit::align<intptr_t>(ptr, alignement);
            }
            VirtualFree(LPVOID(ptr), size, MEM_RELEASE);
            if (unsuseful_reservation++ == unsuseful_reservation_limit) {
               printf("! %d unsuseful reservation detected !\n", unsuseful_reservation_limit);
               unsuseful_reservation = 0;
            }
         }

         // Try again for aligned base after found ptr
         base += alignement;
      }
      return 0;
   }

   uintptr_t AllocateMemory(uintptr_t base, uintptr_t limit, uintptr_t size, uintptr_t alignement) {
      return AcquireMemory(base, limit, size, alignement, MEM_COMMIT, PAGE_READWRITE);
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

   Thread::Thread() {
      this->d0 = 0;
      this->d1 = 0;
   }
   Thread::Thread(Thread&& x) {
      this->d0 = x.d0;
      this->d1 = x.d1;
      x.d0 = 0;
      x.d1 = 0;
   }
   void Thread::operator = (Thread&& x) {
      this->d0 = x.d0;
      this->d1 = x.d1;
      x.d0 = 0;
      x.d1 = 0;
   }
   Thread::~Thread() {
      this->Clear();
   }
   uint64_t Thread::GetID() {
      return this->d0;
   }
   bool Thread::IsCurrent() {
      return this->d0 == ::GetCurrentThreadId();
   }
   void Thread::Suspend() {
      ::SuspendThread(HANDLE(this->d1));
   }
   void Thread::Resume() {
      ::ResumeThread(HANDLE(this->d1));
   }
   Thread::operator bool() {
      return this->d1 != 0;
   }
   void Thread::Clear() {
      if (this->d1) {
         CloseHandle(HANDLE(this->d1));
         this->d0 = 0;
         this->d1 = 0;
      }
   }
   Thread Thread::current() {
      Thread t;
      t.d0 = uint64_t(::GetCurrentThreadId());
      t.d1 = uint64_t(OpenThread(THREAD_ALL_ACCESS, false, ::GetCurrentThreadId()));
      return t;
   }
}
