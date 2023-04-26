#include <ins/memory/malloc.h>
#include <ins/memory/controller.h>

using namespace ins;
using namespace ins::mem;

void* ins_malloc(size_t size) {
   if (auto context = mem::CurrentContext) return context->AllocateUnmanaged(0, size);
   else return mem::DefaultContext->AllocateUnmanaged(0, size);
}

void* ins_calloc(size_t count, size_t size) {
   return malloc(count * size);
}

void ins_free(void* ptr) {
   ObjectLocation(ptr).Free(mem::CurrentContext);
}

size_t ins_msize(void* ptr, tp_ins_msize default_msize) {
   ObjectInfos infos(ptr);
   if (infos.object) {
      return infos.GetUsableSize();
   }
   else if (default_msize) {
      return default_msize(ptr);
   }
   return 0;
}

void* ins_realloc(void* ptr, size_t size, tp_ins_realloc default_realloc) {
   ObjectInfos infos(ptr);
   if (infos.object) {
      if (size == 0) {
         ObjectLocation(ptr).Release(mem::CurrentContext);
         return 0;
      }
      else if (size > infos.GetUsableSize()) {
         void* new_ptr = ins_malloc(size);
         memcpy(new_ptr, ptr, infos.GetUsableSize());
         ObjectLocation(ptr).Release(mem::CurrentContext);
         return new_ptr;
      }
      else {
         return ptr;
      }
   }
   else {
      if (default_realloc) {
         return default_realloc(ptr, size);
      }
      else {
         void* new_ptr = ins_malloc(size);
         __try { memcpy(new_ptr, ptr, size); }
         __except (1) {}
         printf("sat cannot realloc unkown buffer\n");
         return new_ptr;
      }
   }
   return 0;
}

ins_memory_stats_t ins_get_memory_stats() {
   return ins_memory_stats_t(); // TODO
}
