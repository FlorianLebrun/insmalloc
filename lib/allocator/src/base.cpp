#include "./base.h"
#include "./allocators/ZonedBuddy/index.h"
#include "./allocators/LargeObject/index.h"
#include <sat/threads/thread.hpp>
#include <algorithm>
#include <iostream>

bool sat::enableObjectTracing = 0;
bool sat::enableObjectStackTracing = 0;
bool sat::enableObjectTimeTracing = 0;

sat::SpinLock sat::heaps_lock;
sat::GlobalHeap* sat::heaps_list = 0;
sat::GlobalHeap* sat::heaps_table[256] = { 0 };

static sat::tp_malloc _sat_default_malloc = 0;
static sat::tp_realloc _sat_default_realloc = 0;
static sat::tp_msize _sat_default_msize = 0;
static sat::tp_free _sat_default_free = 0;
static bool is_initialized = false;
static bool is_patched = false;

struct CurrentLocalHeap {
private:
   static __declspec(thread) sat::LocalHeap* __current_local_heap;
   static sat::LocalHeap* _fetch() {
      _ASSERT(is_initialized);
      if (!__current_local_heap) {
         auto thread = sat::Thread::current();
         __current_local_heap = thread->getObject<sat::LocalHeap>();
      }
      return __current_local_heap;
   }
   static sat::LocalHeap* _install() {
      _ASSERT(is_initialized);
      if (!__current_local_heap) {
         if (auto thread = sat::Thread::current()) {
            __current_local_heap = thread->getObject<sat::LocalHeap>();
            if (!__current_local_heap) {
               auto global_heap = thread->getObject<sat::GlobalHeap>();
               if (!global_heap) {
                  global_heap = sat::heaps_table[0];
                  global_heap->retain();
                  thread->setObject<sat::GlobalHeap>(global_heap);
               }
               __current_local_heap = global_heap->createLocal();
            }
         }
      }
      return __current_local_heap;
   }
public:
   __forceinline static sat::LocalHeap* check() {
      if (!__current_local_heap) return _fetch();
      return __current_local_heap;
   }
   __forceinline static sat::LocalHeap* get() {
      if (!__current_local_heap) return _install();
      else return __current_local_heap;
   }
};

__declspec(thread) sat::LocalHeap* CurrentLocalHeap::__current_local_heap = nullptr;

sat::LocalHeap* sat::getLocalHeap() {
   return CurrentLocalHeap::get();
}

void _sat_set_default_allocator(
   sat::tp_malloc malloc,
   sat::tp_realloc realloc,
   sat::tp_msize msize,
   sat::tp_free free)
{
   _sat_default_malloc = malloc;
   _sat_default_realloc = realloc;
   _sat_default_msize = msize;
   _sat_default_free = free;
}

void sat_init_process() {
   if (!is_initialized) {
      is_initialized = true;
      sat::MemoryTableController::self.initialize();
      sat::initializeHeaps();
   }
   else printf("sat library is initalized more than once.");
}

void sat_patch_default_allocator() {
   extern void PatchWindowsFunctions();
   if (!is_initialized) {
      printf("Critical: Cannot patch before sat_init_process()");
      exit(1);
   }
   else if (!is_patched) {
      is_patched = true;
      PatchWindowsFunctions();
   }
   else printf("default allocator is patched more than once.");
}

void sat_terminate_process() {
   // COULD BE USED
}

void sat_attach_current_thread() {
}

void sat_dettach_current_thread() {
   if (sat::Thread* thread = sat::Thread::current()) {
      std::cout << "thread:" << std::this_thread::get_id() << " is dettached.\n";
      //sat::current_thread = 0;
      thread->release();
   }
}

void sat_flush_cache() {
   if (auto thread = sat::Thread::current()) {
      if (auto local_heap = thread->getObject<sat::LocalHeap>()) {
         local_heap->flushCache();
      }
      if (auto global_heap = thread->getObject<sat::GlobalHeap>()) {
         global_heap->flushCache();
      }
   }
}

void* sat_malloc_ex(size_t size, uint64_t meta) {
   if (auto local_heap = CurrentLocalHeap::get()) {
      void* ptr = local_heap->allocateWithMeta(size, meta);
#ifdef _DEBUG
      memset(ptr, 0xdd, size);
#endif
      return ptr;
   }
   return 0;
}

void* sat_malloc(size_t size) {
   if (auto local_heap = CurrentLocalHeap::get()) {
      void* ptr = local_heap->allocate(size);
#ifdef _DEBUG
      memset(ptr, 0xdd, size);
#endif
      return ptr;
   }
   return 0;
}

void* sat_calloc(size_t count, size_t size) {
   return malloc(count * size);
}

bool sat_get_metadata(void* ptr, sat::tObjectMetaData& meta) {
   return false;
}

size_t sat_msize(void* ptr, sat::tp_msize default_msize) {
   sat::tObjectInfos infos;
   if (sat_get_address_infos(ptr, &infos)) {
      return infos.size;
   }
   else {
      if (default_msize) return default_msize(ptr);
      else if (_sat_default_msize) return _sat_default_msize(ptr);
      else printf("sat cannot find size of unkown buffer\n");
   }
   return 0;
}

void* sat_realloc(void* ptr, size_t size, sat::tp_realloc default_realloc) {
   sat::tObjectInfos infos;
   if (sat_get_address_infos(ptr, &infos)) {
      if (size == 0) {
         sat_free(ptr);
         return 0;
      }
      else if (size > infos.size) {
         void* new_ptr = sat_malloc(size);
         memcpy(new_ptr, ptr, infos.size);
         sat_free(ptr);
         return new_ptr;
      }
      else {
         return ptr;
      }
   }
   else {
      if (default_realloc) return default_realloc(ptr, size);
      else if (_sat_default_realloc) return _sat_default_realloc(ptr, size);
      else {
         void* new_ptr = sat_malloc(size);
         __try { memcpy(new_ptr, ptr, size); }
         __except (1) {}
         printf("sat cannot realloc unkown buffer\n");
         return new_ptr;
      }
   }
}

void sat_free(void* ptr, sat::tp_free default_free) {
   uintptr_t index = uintptr_t(ptr) >> sat::memory::cSegmentSizeL2;
   if (index < sat::MemoryTableController::self.length) {
      auto entry = sat::MemoryTableController::table[index];
      entry->free(index, uintptr_t(ptr));
   }
   else printf("sat_free: out of range pointer %.12llX\n", int64_t(ptr));
}

bool sat_get_address_infos(void* ptr, sat::tpObjectInfos infos) {
   uintptr_t index = uintptr_t(ptr) >> sat::memory::cSegmentSizeL2;
   if (index < sat::MemoryTableController::self.length) {
      auto entry = sat::MemoryTableController::table[index];
      return entry->getAddressInfos(index, uintptr_t(ptr), infos);
   }
   return false;
}

sat::Heap* sat::getHeap(int id) {
   if (id >= 0 && id < 256) return sat::heaps_table[id];
   return 0;
}

sat::Heap* sat::createHeap(tHeapType type, const char* name) {
   heaps_lock.lock();

   // Get heap id
   int heapId = -1;
   for (int i = 0; i < 256; i++) {
      if (!sat::heaps_table[i]) {
         heapId = i;
         break;
      }
   }

   // Create heap
   GlobalHeap* heap = 0;
   if (heapId >= 0) {
      switch (type) {
      case tHeapType::COMPACT_HEAP_TYPE:
         heap = sat::createHeapCompact(name);
         break;
      }
      heap->heapID = heapId;
      sat::heaps_table[heapId] = heap;
   }

   heaps_lock.unlock();
   return heap;
}

void sat::initializeHeaps() {
   // Init analysis features
   sat::enableObjectTracing = false;
   sat::enableObjectStackTracing = false;
   sat::enableObjectTimeTracing = false;

   // Init heap table
   sat::heaps_list = 0;
   memset(sat::heaps_table, sizeof(sat::heaps_table), 0);

   // Create heap 0
   sat::createHeap(sat::tHeapType::COMPACT_HEAP_TYPE, "main");
}
