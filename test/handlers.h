#pragma once
#include <mimalloc.h>
#include <ins/binary/alignment.h>
#include <ins/memory/space.h>
#include <ins/memory/heap.h>
#include <ins/memory/space.h>
#include <ins/hooks.h>
#include "./utils.h"

struct no_malloc_handler {
   static const char* name() {
      return "no-malloc";
   }
   __forceinline  static void* malloc(size_t) {
      static char bytes[2048];
      ins::__nop();
      return bytes;
   }
   static void free(void*) {
      ins::__nop();
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
         context = ins_get_heap().AcquireContext();
      }
   }
   static const char* name() {
      return "ins-malloc";
   }
   static void* malloc(size_t s) {
      return context->AllocateBuffer(s);
   }
   static void free(void* p) {
      return context->FreeBuffer(uintptr_t(p));
   }
   static bool check(void* p) {
      //ins::ObjectLocation loc(uintptr_t(p));
      //return loc.object != 0;
      return true;
   }
};
