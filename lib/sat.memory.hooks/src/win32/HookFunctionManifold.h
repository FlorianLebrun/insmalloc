#pragma once
#include <stdint.h>

namespace sat {
   struct HookFunction {
      static const int c_MaxRedirection = 10;

      typedef void* FuncPtr;

      template <class Impl>
      struct Context;

      struct HookPoint {
         FuncPtr target = 0;
         FuncPtr original_ = 0;
         FuncPtr redirect = 0;
      };

      intptr_t index = -1;
      HookPoint redirections[c_MaxRedirection];
      const char* const name; // mangled name
      FuncPtr static_fn;

      HookFunction(const char* name, FuncPtr static_fn)
         : name(name), static_fn(static_fn) {
      }
      template<typename Fn>
      Fn* GetOriginal(int index) {
         return (Fn*)this->redirections[index].original_;
      }
      bool IsRedirected(FuncPtr ptr) {
         for (int i = 0; i < c_MaxRedirection; i++) {
            if (this->redirections[i].redirect == ptr) return true;
         }
         return false;
      }
      HookPoint* GetRedirection(FuncPtr target) {
         int i = 0;
         while (i < c_MaxRedirection && this->redirections[i].target) {
            auto& redir = this->redirections[i];
            if (redir.target == target || redir.redirect == target || redir.original_ == target) {
               return &redir;
            }
            i++;
         }
         if (i < c_MaxRedirection) {
            this->redirections[i].target = target;
            return &this->redirections[i];
         }
         else {
            throw "hooking redirection overflow";
         }
      }
   };

   template <class Impl>
   struct HookFunction::Context : HookFunction {
      Context(const char* name, FuncPtr static_fn) : HookFunction(name, static_fn) {
         this->redirections[0].redirect = (FuncPtr)&Impl::Proc<0>;
         this->redirections[1].redirect = (FuncPtr)&Impl::Proc<1>;
         this->redirections[2].redirect = (FuncPtr)&Impl::Proc<2>;
         this->redirections[3].redirect = (FuncPtr)&Impl::Proc<3>;
         this->redirections[4].redirect = (FuncPtr)&Impl::Proc<4>;
         this->redirections[5].redirect = (FuncPtr)&Impl::Proc<5>;
         this->redirections[6].redirect = (FuncPtr)&Impl::Proc<6>;
         this->redirections[7].redirect = (FuncPtr)&Impl::Proc<7>;
         this->redirections[8].redirect = (FuncPtr)&Impl::Proc<8>;
         this->redirections[9].redirect = (FuncPtr)&Impl::Proc<9>;
      }
   };

   struct HookFunctionManifold {
      HookFunction* hooks[100];
      int numHooks = 0;
      void Add(HookFunction& hook) {
         hook.index = numHooks;
         hooks[numHooks++] = &hook;
      }
      void PatchModule(void* BaseOfDll, size_t SizeOfImage);
      void PatchExecutable();
      void PatchAllModules();
   };

}
