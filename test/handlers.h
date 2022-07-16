#pragma once
#include <mimalloc.h>
#include <ins/binary/alignment.h>
#include <ins/memory/space.h>
#include <ins/memory/context.h>
#include <ins/memory/gc.h>
#include <ins/hooks.h>
#include "./utils.h"

struct no_malloc_handler {
   static const char* name() {
      return "no-malloc";
   }
   __forceinline  static void* malloc(size_t) {
      __nop();
      return (void*)-1;
   }
   static void free(void*) {
      __nop();
   }
   static bool check(void* p) {
      return true;
   }
};

struct default_malloc_handler {
   static const char* name() {
      return "default-malloc";
   }
   static void* malloc(size_t s) {
      return ::malloc(s);
   }
   static void free(void* p) {
      return ::free(p);
   }
   static bool check(void* p) {
      return true;
   }
};

struct mi_malloc_handler {
   static const char* name() {
      return "mi-malloc";
   }
   static void* malloc(size_t s) {
      return mi_malloc(s);
   }
   static void free(void* p) {
      return mi_free(p);
   }
   static bool check(void* p) {
      return true;
   }
};

struct ins_malloc_handler {
   static ins::MemoryContext* context;
   static void init() {
      if (!context) {
         context = new ins::MemoryContext(new ins::MemorySpace());
      }
   }
   static const char* name() {
      return "ins-malloc";
   }
   static void* malloc(size_t s) {
      return context->allocateBlock(s);
   }
   static void free(void* p) {
      return context->disposeBlock(uintptr_t(p));
   }
   static bool check(void* p) {
      ins::BlockLocation block(context->space, uintptr_t(p));
      return block.descriptor || block.index;
   }
};
