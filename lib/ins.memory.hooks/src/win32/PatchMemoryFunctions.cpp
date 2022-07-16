
#include <windows.h>
#include <stdio.h>
#include <malloc.h>       // for _msize and _expand
#include <psapi.h>        // for EnumProcessModules, GetModuleInformation, etc.
#include <set>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>
#include "./HookFunctionManifold.h"

std::atomic<intptr_t> _windows_loader_working;

// These are hard-coded, unfortunately. :-( They are also probably
// compiler specific.  See get_mangled_names.cc, in this directory,
// for instructions on how to update these names for your compiler.
#ifdef _WIN64
const char kMangledNew[] = "??2@YAPEAX_K@Z";
const char kMangledNewArray[] = "??_U@YAPEAX_K@Z";
const char kMangledDelete[] = "??3@YAXPEAX@Z";
const char kMangledDeleteArray[] = "??_V@YAXPEAX@Z";
const char kMangledNewNothrow[] = "??2@YAPEAX_KAEBUnothrow_t@std@@@Z";
const char kMangledNewArrayNothrow[] = "??_U@YAPEAX_KAEBUnothrow_t@std@@@Z";
const char kMangledDeleteNothrow[] = "??3@YAXPEAXAEBUnothrow_t@std@@@Z";
const char kMangledDeleteArrayNothrow[] = "??_V@YAXPEAXAEBUnothrow_t@std@@@Z";
#else
const char kMangledNew[] = "??2@YAPAXI@Z";
const char kMangledNewArray[] = "??_U@YAPAXI@Z";
const char kMangledDelete[] = "??3@YAXPAX@Z";
const char kMangledDeleteArray[] = "??_V@YAXPAX@Z";
const char kMangledNewNothrow[] = "??2@YAPAXIABUnothrow_t@std@@@Z";
const char kMangledNewArrayNothrow[] = "??_U@YAPAXIABUnothrow_t@std@@@Z";
const char kMangledDeleteNothrow[] = "??3@YAXPAXABUnothrow_t@std@@@Z";
const char kMangledDeleteArrayNothrow[] = "??_V@YAXPAXABUnothrow_t@std@@@Z";
#endif

namespace {
   using namespace ins;

   static HookFunctionManifold hooks_manifold;

#define __THROW

   struct malloc_hook {
      static HookFunction::Context<malloc_hook> context;
      template<int i>
      static void* Proc(size_t sz) {
         auto original = context.GetOriginal<void* (size_t)>(i);
         return original(sz);
      }
   };

   struct free_hook {
      static HookFunction::Context<free_hook> context;
      template<int i>
      static void Proc(void* ptr) __THROW {
         auto original = context.GetOriginal<void(void*)>(i);
         return original(ptr);
      }
   };

   struct _free_base_hook {
      static HookFunction::Context<_free_base_hook> context;
      template<int i>
      static void Proc(void* ptr) __THROW {
         auto original = context.GetOriginal<void(void*)>(i);
         return original(ptr);
      }
   };

   struct _free_dbg_hook {
      static HookFunction::Context<_free_dbg_hook> context;
      template<int i>
      static void Proc(void* ptr, int block_use) __THROW {
         auto original = context.GetOriginal<void(void*, int)>(i);
         return original(ptr, block_use);
      }
   };

   struct realloc_hook {
      static HookFunction::Context<realloc_hook> context;
      template<int i>
      static void* Proc(void* old_ptr, size_t new_size) __THROW {
         auto original = context.GetOriginal<void* (void*, size_t)>(i);
         return original(old_ptr, new_size);
      }
   };

   struct calloc_hook {
      static HookFunction::Context<calloc_hook> context;
      template<int i>
      static void* Proc(size_t n, size_t elem_size) __THROW {
         auto original = context.GetOriginal<void* (size_t, size_t)>(i);
         return original(n, elem_size);
      }
   };

   struct new_hook {
      static HookFunction::Context<new_hook> context;
      template<int i>
      static void* Proc(size_t size) {
         auto original = context.GetOriginal<void* (size_t)>(i);
         return original(size);
      }
   };

   struct newarray_hook {
      static HookFunction::Context<newarray_hook> context;
      template<int i>
      static void* Proc(size_t size) {
         auto original = context.GetOriginal<void* (size_t)>(i);
         return original(size);
      }
   };

   struct delete_hook {
      static HookFunction::Context<delete_hook> context;
      template<int i>
      static void Proc(void* p) {
         auto original = context.GetOriginal<void(void*)>(i);
         return original(p);
      }
   };

   struct deletearray_hook {
      static HookFunction::Context<deletearray_hook> context;
      template<int i>
      static void Proc(void* p) {
         auto original = context.GetOriginal<void(void*)>(i);
         return original(p);
      }
   };

   struct new_nothrow_hook {
      static HookFunction::Context<new_nothrow_hook> context;
      template<int i>
      static void* Proc(size_t size, const std::nothrow_t& x) __THROW {
         auto original = context.GetOriginal<void* (size_t, const std::nothrow_t&)>(i);
         return original(size, x);
      }
   };

   struct newarray_nothrow_hook {
      static HookFunction::Context<newarray_nothrow_hook> context;
      template<int i>
      static void* Proc(size_t size, const std::nothrow_t& x) __THROW {
         auto original = context.GetOriginal<void* (size_t, const std::nothrow_t&)>(i);
         return original(size, x);
      }
   };

   struct delete_nothrow_hook {
      static HookFunction::Context<delete_nothrow_hook> context;
      template<int i>
      static void Proc(void* p, const std::nothrow_t& x) __THROW {
         auto original = context.GetOriginal<void(void*, const std::nothrow_t&)>(i);
         return original(p, x);
      }
   };

   struct deletearray_nothrow_hook {
      static HookFunction::Context<deletearray_nothrow_hook> context;
      template<int i>
      static void Proc(void* p, const std::nothrow_t& x) __THROW {
         auto original = context.GetOriginal<void(void*, const std::nothrow_t&)>(i);
         return original(p, x);
      }
   };


   struct _msize_hook {
      static HookFunction::Context<_msize_hook> context;
      template<int i>
      static size_t Proc(void* ptr) __THROW {
         auto original = context.GetOriginal<size_t(void*)>(i);
         return original(ptr);
      }
   };

   struct _expand_hook {
      static HookFunction::Context<_expand_hook> context;
      template<int i>
      static void* Proc(void* ptr, size_t size) __THROW {
         return NULL;
      }
   };

   struct _calloc_hook {
      static HookFunction::Context<_calloc_hook> context;
      template<int i>
      static void* Proc(size_t n, size_t elem_size) __THROW {
         auto original = context.GetOriginal<void* (size_t, size_t)>(i);
         return original(n, elem_size);
      }
   };

   struct HeapAlloc_hook {
      static HookFunction::Context<HeapAlloc_hook> context;
      template<int i>
      static LPVOID WINAPI Proc(HANDLE hHeap, DWORD dwFlags, DWORD_PTR dwBytes) {
         auto original = context.GetOriginal<LPVOID WINAPI(HANDLE, DWORD, DWORD_PTR)>(i);
         return original(hHeap, dwFlags, dwBytes);
      }
   };

   struct HeapFree_hook {
      static HookFunction::Context<HeapFree_hook> context;
      template<int i>
      static BOOL WINAPI Proc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) {
         auto original = context.GetOriginal<BOOL WINAPI(HANDLE, DWORD, LPVOID)>(i);
         return original(hHeap, dwFlags, lpMem);
      }
   };

   struct VirtualAllocEx_hook {
      static HookFunction::Context<VirtualAllocEx_hook> context;
      template<int i>
      static LPVOID WINAPI Proc(HANDLE process, LPVOID address, SIZE_T size, DWORD type, DWORD protect) {
         auto original = context.GetOriginal<LPVOID WINAPI(HANDLE, LPVOID, SIZE_T, DWORD, DWORD)>(i);
         return original(process, address, size, type, protect);
      }
   };

   struct VirtualFreeEx_hook {
      static HookFunction::Context<VirtualFreeEx_hook> context;
      template<int i>
      static BOOL WINAPI Proc(HANDLE process, LPVOID address, SIZE_T size, DWORD type) {
         auto original = context.GetOriginal<BOOL WINAPI(HANDLE, LPVOID, SIZE_T, DWORD)>(i);
         return original(process, address, size, type);
      }
   };

   struct MapViewOfFileEx_hook {
      static HookFunction::Context<MapViewOfFileEx_hook> context;
      template<int i>
      static LPVOID WINAPI Proc(
         HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh,
         DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap, LPVOID lpBaseAddress) {
         // For this function pair, you always deallocate the full block of
         // data that you allocate, so NewHook/DeleteHook is the right API.
         auto original = context.GetOriginal<LPVOID WINAPI(HANDLE, DWORD, DWORD, DWORD, SIZE_T, LPVOID)>(i);
         return original(
            hFileMappingObject, dwDesiredAccess, dwFileOffsetHigh,
            dwFileOffsetLow, dwNumberOfBytesToMap, lpBaseAddress);
      }
   };

   struct UnmapViewOfFile_hook {
      static HookFunction::Context<UnmapViewOfFile_hook> context;
      template<int i>
      static BOOL WINAPI Proc(LPCVOID lpBaseAddress) {
         auto original = context.GetOriginal<BOOL WINAPI(LPCVOID)>(i);
         return original(lpBaseAddress);
      }
   };

   struct LoadLibraryExW_hook {
      static HookFunction::Context<LoadLibraryExW_hook> context;
      template<int i>
      static HMODULE WINAPI Proc(LPCWSTR lpFileName, HANDLE hFile, DWORD dwFlags) {
         HMODULE rv;
         // Check to see if the modules is already loaded, flag 0 gets a
         // reference if it was loaded.  If it was loaded no need to call
         // PatchAllModules, just increase the reference count to match
         // what GetModuleHandleExW does internally inside windows.
         if (::GetModuleHandleExW(0, lpFileName, &rv)) {
            return rv;
         }
         else {
            _windows_loader_working++;

            // Not already loaded, so load it.
            auto original = context.GetOriginal<HMODULE WINAPI(LPCWSTR, HANDLE, DWORD)>(i);
            HMODULE hModule = original(lpFileName, hFile, dwFlags);

            MODULEINFO mi;
            if (::GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi))) {
               hooks_manifold.PatchModule(mi.lpBaseOfDll, mi.SizeOfImage);
            }

            _windows_loader_working--;
            return rv;
         }
      }
   };

   struct FreeLibrary_hook {
      static HookFunction::Context<FreeLibrary_hook> context;
      template<int i>
      static BOOL WINAPI Proc(HMODULE hLibModule) {
         auto original = context.GetOriginal<BOOL WINAPI(HMODULE)>(i);
         BOOL rv = original(hLibModule);

         // Check to see if the module is still loaded by passing the base
         // address and seeing if it comes back with the same address.  If it
         // is the same address it's still loaded, so the FreeLibrary() call
         // was a noop, and there's no need to redo the patching.
         HMODULE owner = NULL;
         BOOL result = ::GetModuleHandleExW(
            (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT),
            (LPCWSTR)hLibModule,
            &owner);
         if (result && owner == hLibModule)
            return rv;

         hooks_manifold.PatchAllModules();    // this will fix up the list of patched libraries
         return rv;
      }
   };

   // libc functions
   HookFunction::Context<malloc_hook> malloc_hook::context("malloc", &::malloc);
   HookFunction::Context<free_hook> free_hook::context("free", &::free);
   HookFunction::Context<realloc_hook> realloc_hook::context("realloc", &::realloc);
   HookFunction::Context<calloc_hook> calloc_hook::context("calloc", &::calloc);
   HookFunction::Context<new_hook> new_hook::context(kMangledNew, (void* (*)(size_t))& ::operator new);
   HookFunction::Context<newarray_hook> newarray_hook::context(kMangledNewArray, (void* (*)(size_t))& ::operator new[]);
   HookFunction::Context<delete_hook> delete_hook::context(kMangledDelete, (void(*)(void*))& ::operator delete);
   HookFunction::Context<deletearray_hook> deletearray_hook::context(kMangledDeleteArray, (void(*)(void*))& ::operator delete[]);
   HookFunction::Context<new_nothrow_hook> new_nothrow_hook::context(kMangledNewNothrow, (void* (*)(size_t, struct std::nothrow_t const&))& ::operator new);
   HookFunction::Context<newarray_nothrow_hook> newarray_nothrow_hook::context(kMangledNewArrayNothrow, (void* (*)(size_t, struct std::nothrow_t const&))& ::operator new[]);
   HookFunction::Context<delete_nothrow_hook> delete_nothrow_hook::context(kMangledDeleteNothrow, (void(*)(void*, struct std::nothrow_t const&))& ::operator delete);
   HookFunction::Context<deletearray_nothrow_hook> deletearray_nothrow_hook::context(kMangledDeleteArrayNothrow, (void(*)(void*, struct std::nothrow_t const&))& ::operator delete[]);
   HookFunction::Context<_msize_hook> _msize_hook::context("_msize", &::_msize);
   HookFunction::Context<_expand_hook> _expand_hook::context("_expand", &::_expand);
   HookFunction::Context<_calloc_hook> _calloc_hook::context("_calloc_crt", &::calloc);
   HookFunction::Context<_free_base_hook> _free_base_hook::context("_free_base", &::free);
   HookFunction::Context<_free_dbg_hook> _free_dbg_hook::context("_free_dbg", &::free);

   // kernel32 functions
   HookFunction::Context<HeapAlloc_hook> HeapAlloc_hook::context("HeapAlloc", &::HeapAlloc);
   HookFunction::Context<HeapFree_hook> HeapFree_hook::context("HeapFree", &::HeapFree);
   HookFunction::Context<VirtualAllocEx_hook> VirtualAllocEx_hook::context("VirtualAllocEx", &::VirtualAllocEx);
   HookFunction::Context<VirtualFreeEx_hook> VirtualFreeEx_hook::context("VirtualFreeEx", &::VirtualFreeEx);
   HookFunction::Context<MapViewOfFileEx_hook> MapViewOfFileEx_hook::context("MapViewOfFileEx", &::MapViewOfFileEx);
   HookFunction::Context<UnmapViewOfFile_hook> UnmapViewOfFile_hook::context("UnmapViewOfFile", &::UnmapViewOfFile);
   HookFunction::Context<LoadLibraryExW_hook> LoadLibraryExW_hook::context("LoadLibraryExW", &::LoadLibraryExW);
   HookFunction::Context<FreeLibrary_hook> FreeLibrary_hook::context("FreeLibrary", &::FreeLibrary);
}

namespace ins {

   void PatchMemoryFunctions() {

      // libc functions
      hooks_manifold.Add(malloc_hook::context);
      hooks_manifold.Add(free_hook::context);
      hooks_manifold.Add(realloc_hook::context);
      hooks_manifold.Add(calloc_hook::context);
      hooks_manifold.Add(new_hook::context);
      hooks_manifold.Add(newarray_hook::context);
      hooks_manifold.Add(delete_hook::context);
      hooks_manifold.Add(deletearray_hook::context);
      hooks_manifold.Add(new_nothrow_hook::context);
      hooks_manifold.Add(newarray_nothrow_hook::context);
      hooks_manifold.Add(delete_nothrow_hook::context);
      hooks_manifold.Add(deletearray_nothrow_hook::context);
      hooks_manifold.Add(_msize_hook::context);
      hooks_manifold.Add(_expand_hook::context);
      hooks_manifold.Add(_calloc_hook::context);
      hooks_manifold.Add(_free_base_hook::context);
      hooks_manifold.Add(_free_dbg_hook::context);

      // kernel32 functions
      hooks_manifold.Add(HeapAlloc_hook::context);
      hooks_manifold.Add(HeapFree_hook::context);
      hooks_manifold.Add(VirtualAllocEx_hook::context);
      hooks_manifold.Add(VirtualFreeEx_hook::context);
      hooks_manifold.Add(MapViewOfFileEx_hook::context);
      hooks_manifold.Add(UnmapViewOfFile_hook::context);
      hooks_manifold.Add(LoadLibraryExW_hook::context);
      hooks_manifold.Add(FreeLibrary_hook::context);

      hooks_manifold.PatchAllModules();
   }
}

