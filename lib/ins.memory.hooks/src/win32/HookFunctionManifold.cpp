#include "./HookFunctionManifold.h"

#include <windows.h>
#include <psapi.h>        // for EnumProcessModules, GetModuleInformation, etc.
#include <atomic>
#include <mutex>
#include "patcher/preamble_patcher.h"

using sidestep::PreamblePatcher;
using namespace ins::mem;

#ifdef _MSC_VER
#pragma comment(lib, "Psapi.lib")
#endif

typedef void* FuncPtr;

static std::mutex patch_all_modules_lock;

static void FillExecutableHookTargetAddresses(HookFunctionManifold& manifold, FuncPtr rgProcAddresses[]) {
   for (int i = 0; i < manifold.numHooks; i++) {
      rgProcAddresses[i] = manifold.hooks[i]->static_fn;
   }
}

static void FillModuleHookTargetAddresses(HookFunctionManifold& manifold, void* BaseOfDll, size_t SizeOfImage, FuncPtr rgProcAddresses[]) {
   LPVOID modBaseAddr = BaseOfDll;
   LPVOID modEndAddr = (char*)BaseOfDll + SizeOfImage;
   for (int i = 0; i < manifold.numHooks; i++) {
      FARPROC target = ::GetProcAddress(reinterpret_cast<const HMODULE>(BaseOfDll), manifold.hooks[i]->name);
      // Sometimes a DLL forwards a function to a function in another
      // DLL.  We don't want to patch those forwarded functions --
      // they'll get patched when the other DLL is processed.
      if (target >= modBaseAddr && target < modEndAddr)
         rgProcAddresses[i] = target;
      else
         rgProcAddresses[i] = NULL;
   }
}

static bool PopulateModuleHooksWindowsFn(HookFunctionManifold& manifold, const FuncPtr rgProcAddresses[], FuncPtr windows_fn_[]) {

   // First, store the location of the function to patch before
   // patching it.  If none of these functions are found in the module,
   // then this module has no libc in it, and we just return false.
   for (int i = 0; i < manifold.numHooks; i++) {
      const FuncPtr fn = rgProcAddresses[i];
      if (fn) {
         windows_fn_[i] = PreamblePatcher::ResolveTarget(fn);
      }
      else {
         windows_fn_[i] = NULL;
      }
   }

   // Some modules use the same function pointer for new and new[].  If
   // we find that, set one of the pointers to NULL so we don't double-
   // patch.  Same may happen with new and nothrow-new, or even new[]
   // and nothrow-new.  It's easiest just to check each fn-ptr against
   // every other.
   for (int i = 0; i < manifold.numHooks; i++) {
      for (int j = i + 1; j < manifold.numHooks; j++) {
         if (windows_fn_[i] == windows_fn_[j]) {
            // We NULL the later one (j), so as to minimize the chances we
            // NULL kFree and kRealloc.  See comments below.  This is fragile!
            windows_fn_[j] = NULL;
         }
      }
   }

   for (int i = 0; i < manifold.numHooks; i++) {
      if (windows_fn_[i]) return true;
   }
   return false;
}

static bool PopulateModuleHooks(HookFunctionManifold& manifold, const FuncPtr windows_fn_[]) {
   for (int i = 0; i < manifold.numHooks; i++) {
      if (windows_fn_[i] && !manifold.hooks[i]->IsRedirected(windows_fn_[i])) {
         // if origstub_fn_ is not NULL, it's left around from a previous
         // patch.  We need to set it to NULL for the new Patch call.
         //
         // Note that origstub_fn_ was logically freed by
         // PreamblePatcher::Unpatch, so we don't have to do anything
         // about it.
         auto redirection = manifold.hooks[i]->GetRedirection(windows_fn_[i]);
         auto result = PreamblePatcher::Patch(redirection->target, redirection->redirect, &redirection->original_);
         _ASSERT(result == sidestep::SIDESTEP_SUCCESS);
      }
   }
   return true;
}

void HookFunctionManifold::PatchModule(void* BaseOfDll, size_t SizeOfImage) {
   auto rgProcAddresses = (FuncPtr*)alloca(sizeof(FuncPtr) * this->numHooks);
   auto windows_fn_ = (FuncPtr*)alloca(sizeof(FuncPtr) * this->numHooks);
   FillModuleHookTargetAddresses(*this, BaseOfDll, SizeOfImage, rgProcAddresses);
   if (PopulateModuleHooksWindowsFn(*this, rgProcAddresses, windows_fn_)) {
      PopulateModuleHooks(*this, windows_fn_);
   }
}

void HookFunctionManifold::PatchExecutable() {
   auto rgProcAddresses = (FuncPtr*)alloca(sizeof(FuncPtr) * this->numHooks);
   auto windows_fn_ = (FuncPtr*)alloca(sizeof(FuncPtr) * this->numHooks);
   FillExecutableHookTargetAddresses(*this, rgProcAddresses);
   if (PopulateModuleHooksWindowsFn(*this, rgProcAddresses, windows_fn_)) {
      PopulateModuleHooks(*this, windows_fn_);
   }
}

void HookFunctionManifold::PatchAllModules() {
   const int kMaxModules = 8182; // The maximum number of modules supported

   const HANDLE hCurrentProcess = GetCurrentProcess();
   DWORD num_modules = 0;
   HMODULE hModules[kMaxModules];  // max # of modules we support in one process
   if (!::EnumProcessModules(hCurrentProcess, hModules, sizeof(hModules),
      &num_modules)) {
      num_modules = 0;
   }

   // EnumProcessModules actually set the bytes written into hModules,
   // so we need to divide to make num_modules actually be a module-count.
   num_modules /= sizeof(*hModules);
   if (num_modules >= kMaxModules) {
      printf("PERFTOOLS ERROR: Too many modules in this executable to try"
         " to patch them all (if you need to, raise kMaxModules in"
         " patch_functions.cc).\n");
      num_modules = kMaxModules;
   }

   {
      std::lock_guard<std::mutex> guard(patch_all_modules_lock);
      // Now that we know what modules are new, let's get the info we'll
      // need to patch them.  Note this *cannot* be done while holding the
      // lock, since it needs to make windows calls (see the lock-inversion
      // comments before the definition of patch_all_modules_lock).
      for (int i = 0; i < num_modules; i++) {
         MODULEINFO mi;
         if (::GetModuleInformation(hCurrentProcess, hModules[i], &mi, sizeof(mi))) {
            this->PatchModule(mi.lpBaseOfDll, mi.SizeOfImage);
         }
      }

      this->PatchExecutable();
   }

}
