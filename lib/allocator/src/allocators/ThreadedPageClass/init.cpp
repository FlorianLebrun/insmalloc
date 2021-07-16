/* ----------------------------------------------------------------------------
Copyright (c) 2018-2021, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "alloc.h"
#include "alloc-internal.h"

#include <string.h>  // memcpy, memset
#include <stdlib.h>  // atexit

// Empty page used to initialize the small free pages array
const mpc_empty_page_s _mpc_page_empty;

#define MPC_PAGE_EMPTY() ((mpc_page_t*)&_mpc_page_empty)

#define MPC_SMALL_PAGES_EMPTY  { MPC_INIT128(MPC_PAGE_EMPTY), MPC_PAGE_EMPTY() }

// Empty page queues for every bin
#define QNULL(sz)  { NULL, NULL, (sz)*sizeof(uintptr_t) }
#define MPC_PAGE_QUEUES_EMPTY \
  { QNULL(1), \
    QNULL(     1), QNULL(     2), QNULL(     3), QNULL(     4), QNULL(     5), QNULL(     6), QNULL(     7), QNULL(     8), /* 8 */ \
    QNULL(    10), QNULL(    12), QNULL(    14), QNULL(    16), QNULL(    20), QNULL(    24), QNULL(    28), QNULL(    32), /* 16 */ \
    QNULL(    40), QNULL(    48), QNULL(    56), QNULL(    64), QNULL(    80), QNULL(    96), QNULL(   112), QNULL(   128), /* 24 */ \
    QNULL(   160), QNULL(   192), QNULL(   224), QNULL(   256), QNULL(   320), QNULL(   384), QNULL(   448), QNULL(   512), /* 32 */ \
    QNULL(   640), QNULL(   768), QNULL(   896), QNULL(  1024), QNULL(  1280), QNULL(  1536), QNULL(  1792), QNULL(  2048), /* 40 */ \
    QNULL(  2560), QNULL(  3072), QNULL(  3584), QNULL(  4096), QNULL(  5120), QNULL(  6144), QNULL(  7168), QNULL(  8192), /* 48 */ \
    QNULL( 10240), QNULL( 12288), QNULL( 14336), QNULL( 16384), QNULL( 20480), QNULL( 24576), QNULL( 28672), QNULL( 32768), /* 56 */ \
    QNULL( 40960), QNULL( 49152), QNULL( 57344), QNULL( 65536), QNULL( 81920), QNULL( 98304), QNULL(114688), QNULL(131072), /* 64 */ \
    QNULL(163840), QNULL(196608), QNULL(229376), QNULL(262144), QNULL(327680), QNULL(393216), QNULL(458752), QNULL(524288), /* 72 */ \
    QNULL(MPC_LARGE_OBJ_WSIZE_MAX + 1  /* 655360, Huge queue */), \
    QNULL(MPC_LARGE_OBJ_WSIZE_MAX + 2) /* Full queue */ }

#define MPC_STAT_COUNT_NULL()  {0,0,0,0}

// Empty statistics
#define MPC_STAT_COUNT_END_NULL()

#define MPC_STATS_NULL  \
  MPC_STAT_COUNT_NULL(), MPC_STAT_COUNT_NULL(), \
  MPC_STAT_COUNT_NULL(), MPC_STAT_COUNT_NULL(), \
  MPC_STAT_COUNT_NULL(), MPC_STAT_COUNT_NULL(), \
  MPC_STAT_COUNT_NULL(), MPC_STAT_COUNT_NULL(), \
  MPC_STAT_COUNT_NULL(), MPC_STAT_COUNT_NULL(), \
  MPC_STAT_COUNT_NULL(), MPC_STAT_COUNT_NULL(), \
  MPC_STAT_COUNT_NULL(), MPC_STAT_COUNT_NULL(), \
  { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },     \
  { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } \
  MPC_STAT_COUNT_END_NULL()

// --------------------------------------------------------
// Statically allocate an empty heap as the initial
// thread local value for the default heap,
// and statically allocate the backing heap for the main
// thread so it can function without doing any allocation
// itself (as accessing a thread local for the first time
// may lead to allocation itself on some platforms)
// --------------------------------------------------------

mpc_decl_cache_align const mpc_heap_t _mpc_heap_empty = {
  NULL,
  MPC_SMALL_PAGES_EMPTY,
  MPC_PAGE_QUEUES_EMPTY,
  ATOMIC_VAR_INIT(NULL),
  0,                // tid
  0,                // page count
  MPC_BIN_FULL, 0,   // page retired min/max
  NULL,             // next
  false
};

// the thread-local default heap for allocation
mpc_decl_thread mpc_heap_t* _mpc_heap_default = (mpc_heap_t*)&_mpc_heap_empty;

extern mpc_heap_t _mpc_heap_main;

static mpc_tld_t tld_main = {
  0, false,
  &_mpc_heap_main, &_mpc_heap_main,
  { 0 },  // os
};

mpc_heap_t _mpc_heap_main = {
  &tld_main,
  MPC_SMALL_PAGES_EMPTY,
  MPC_PAGE_QUEUES_EMPTY,
  ATOMIC_VAR_INIT(NULL),
  0,                // thread id
  0,                // page count
  MPC_BIN_FULL, 0,   // page retired min/max
  NULL,             // next heap
  false             // can reclaim
};

bool _mpc_process_is_initialized = false;  // set to `true` in `mpc_process_init`.

static void mpc_heap_main_init(void) {
   if (_mpc_heap_main.thread_id == 0) {
      _mpc_heap_main.thread_id = _mpc_thread_id();
   }
}

mpc_heap_t* _mpc_heap_main_get(void) {
   mpc_heap_main_init();
   return &_mpc_heap_main;
}


/* -----------------------------------------------------------
  Initialization and freeing of the thread local heaps
----------------------------------------------------------- */

// note: in x64 in release build `sizeof(mpc_thread_data_t)` is under 4KiB (= OS page size).
typedef struct mpc_thread_data_s {
   mpc_heap_t  heap;  // must come first due to cast in `_mpc_heap_done`
   mpc_tld_t   tld;
} mpc_thread_data_t;

// Initialize the thread local default heap, called from `mpc_thread_init`
static bool _mpc_heap_init(void) {
   if (mpc_heap_is_initialized(mpc_get_default_heap())) return true;
   if (_mpc_is_main_thread()) {
      // the main heap is statically allocated
      mpc_heap_main_init();
      _mpc_heap_set_default_direct(&_mpc_heap_main);
   }
   else {
      // use `_mpc_os_alloc` to allocate directly from the OS
      mpc_thread_data_t* td = (mpc_thread_data_t*)sat::system_object::allocSystemBuffer(sizeof(mpc_thread_data_t));
      memset(td, 0, sizeof(mpc_thread_data_t));
      mpc_tld_t* tld = &td->tld;
      mpc_heap_t* heap = &td->heap;
      _mpc_memcpy_aligned(heap, &_mpc_heap_empty, sizeof(*heap));
      heap->thread_id = _mpc_thread_id();
      heap->tld = tld;
      tld->heap_backing = heap;
      tld->heaps = heap;
      _mpc_heap_set_default_direct(heap);
   }
   return false;
}

// Free the thread local default heap (called from `mpc_thread_done`)
static bool _mpc_heap_done(mpc_heap_t* heap) {
   if (!mpc_heap_is_initialized(heap)) return true;

   // reset default heap
   _mpc_heap_set_default_direct(_mpc_is_main_thread() ? &_mpc_heap_main : (mpc_heap_t*)&_mpc_heap_empty);

   // switch to backing heap
   heap = heap->tld->heap_backing;
   if (!mpc_heap_is_initialized(heap)) return false;

   // delete all non-backing heaps in this thread
   mpc_heap_t* curr = heap->tld->heaps;
   while (curr != NULL) {
      mpc_heap_t* next = curr->next; // save `next` as `curr` will be freed
      if (curr != heap) {
         mpc_heap_delete(curr);
      }
      curr = next;
   }

   // collect if not the main thread
   if (heap != &_mpc_heap_main) {
      _mpc_heap_collect_abandon(heap);
   }

   // free if not the main thread
   if (heap != &_mpc_heap_main) {
      sat_free(heap);
   }
#if 0  
   // never free the main thread even in debug mode; if a dll is linked statically with alloc,
   // there may still be delete/free calls after the mpc_fls_done is called. Issue #207
   else {
      _mpc_heap_destroy_pages(heap);
   }
#endif
   return false;
}



// --------------------------------------------------------
// Try to run `mpc_thread_done()` automatically so any memory
// owned by the thread but not yet released can be abandoned
// and re-owned by another thread.
//
// 1. windows dynamic library:
//     call from DllMain on DLL_THREAD_DETACH
// 2. windows static library:
//     use `FlsAlloc` to call a destructor when the thread is done
// 3. unix, pthreads:
//     use a pthread key to call a destructor when a pthread is done
//
// In the last two cases we also need to call `mpc_process_init`
// to set up the thread local keys.
// --------------------------------------------------------

static void _mpc_thread_done(mpc_heap_t* default_heap);

#ifdef __wasi__
// no pthreads in the WebAssembly Standard Interface
#elif !defined(_WIN32)
#define MPC_USE_PTHREADS
#endif

#if defined(_WIN32) && defined(MPC_SHARED_LIB)
// nothing to do as it is done in DllMain
#elif defined(_WIN32) && !defined(MPC_SHARED_LIB)
// use thread local storage keys to detect thread ending
#include <windows.h>
#include <fibersapi.h>
#if (_WIN32_WINNT < 0x600)  // before Windows Vista 
WINBASEAPI DWORD WINAPI FlsAlloc(_In_opt_ PFLS_CALLBACK_FUNCTION lpCallback);
WINBASEAPI PVOID WINAPI FlsGetValue(_In_ DWORD dwFlsIndex);
WINBASEAPI BOOL  WINAPI FlsSetValue(_In_ DWORD dwFlsIndex, _In_opt_ PVOID lpFlsData);
WINBASEAPI BOOL  WINAPI FlsFree(_In_ DWORD dwFlsIndex);
#endif
static DWORD mpc_fls_key = (DWORD)(-1);
static void NTAPI mpc_fls_done(PVOID value) {
   if (value != NULL) _mpc_thread_done((mpc_heap_t*)value);
}
#elif defined(MPC_USE_PTHREADS)
// use pthread local storage keys to detect thread ending
// (and used with MPC_TLS_PTHREADS for the default heap)
#include <pthread.h>
pthread_key_t _mpc_heap_default_key = (pthread_key_t)(-1);
static void mpc_pthread_done(void* value) {
   if (value != NULL) _mpc_thread_done((mpc_heap_t*)value);
}
#elif defined(__wasi__)
// no pthreads in the WebAssembly Standard Interface
#else
#pragma message("define a way to call mpc_thread_done when a thread is done")
#endif

// Set up handlers so `mpc_thread_done` is called automatically
static void mpc_process_setup_auto_thread_done(void) {
   static bool tls_initialized = false; // fine if it races
   if (tls_initialized) return;
   tls_initialized = true;
#if defined(_WIN32) && defined(MPC_SHARED_LIB)
   // nothing to do as it is done in DllMain
#elif defined(_WIN32) && !defined(MPC_SHARED_LIB)
   mpc_fls_key = FlsAlloc(&mpc_fls_done);
#elif defined(MPC_USE_PTHREADS)
   pthread_key_create(&_mpc_heap_default_key, &mpc_pthread_done);
#endif
   _mpc_heap_set_default_direct(&_mpc_heap_main);
}


bool _mpc_is_main_thread(void) {
   return (_mpc_heap_main.thread_id == 0 || _mpc_heap_main.thread_id == _mpc_thread_id());
}

// This is called from the `mpc_malloc_generic`
void mpc_thread_init(void) mpc_attr_noexcept
{
   // ensure our process has started already
   mpc_process_init();

   // initialize the thread local default heap
   // (this will call `_mpc_heap_set_default_direct` and thus set the
   //  fiber/pthread key to a non-zero value, ensuring `_mpc_thread_done` is called)
   if (_mpc_heap_init()) return;  // returns true if already initialized

}

void mpc_thread_done(void) mpc_attr_noexcept {
   _mpc_thread_done(mpc_get_default_heap());
}

static void _mpc_thread_done(mpc_heap_t* heap) {

   // check thread-id as on Windows shutdown with FLS the main (exit) thread may call this on thread-local heaps...
   if (heap->thread_id != _mpc_thread_id()) return;

   // abandon the thread local heap
   if (_mpc_heap_done(heap)) return;  // returns true if already ran
}

void _mpc_heap_set_default_direct(mpc_heap_t* heap) {
#if defined(MPC_TLS_SLOT)
   mpc_tls_slot_set(MPC_TLS_SLOT, heap);
#elif defined(MPC_TLS_PTHREAD_SLOT_OFS)
   *mpc_tls_pthread_heap_slot() = heap;
#elif defined(MPC_TLS_PTHREAD)
   // we use _mpc_heap_default_key
#else
   _mpc_heap_default = heap;
#endif

   // ensure the default heap is passed to `_mpc_thread_done`
   // setting to a non-NULL value also ensures `mpc_thread_done` is called.
#if defined(_WIN32) && defined(MPC_SHARED_LIB)
  // nothing to do as it is done in DllMain
#elif defined(_WIN32) && !defined(MPC_SHARED_LIB)
   FlsSetValue(mpc_fls_key, heap);
#elif defined(MPC_USE_PTHREADS)
   if (_mpc_heap_default_key != (pthread_key_t)(-1)) {  // can happen during recursive invocation on freeBSD
      pthread_setspecific(_mpc_heap_default_key, heap);
   }
#endif
}


// --------------------------------------------------------
// Run functions on process init/done, and thread init/done
// --------------------------------------------------------
static void mpc_process_done(void);

static bool os_preloading = true;    // true until this module is initialized
static bool mpc_redirected = false;   // true if malloc redirects to mpc_malloc

// Returns true if this module has not been initialized; Don't use C runtime routines until it returns false.
bool _mpc_preloading(void) {
   return os_preloading;
}

bool mpc_is_redirected(void) mpc_attr_noexcept {
   return mpc_redirected;
}

// Communicate with the redirection module on Windows
#if defined(_WIN32) && defined(MPC_SHARED_LIB)
#ifdef __cplusplus
extern "C" {
#endif
   mpc_decl_export void _mpc_redirect_entry(DWORD reason) {
      // called on redirection; careful as this may be called before DllMain
      if (reason == DLL_PROCESS_ATTACH) {
         mpc_redirected = true;
      }
      else if (reason == DLL_PROCESS_DETACH) {
         mpc_redirected = false;
      }
      else if (reason == DLL_THREAD_DETACH) {
         mpc_thread_done();
      }
   }
   __declspec(dllimport) bool mpc_allocator_init(const char** message);
   __declspec(dllimport) void mpc_allocator_done(void);
#ifdef __cplusplus
}
#endif
#else
static bool mpc_allocator_init(const char** message) {
   if (message != NULL) *message = NULL;
   return true;
}
static void mpc_allocator_done(void) {
   // nothing to do
}
#endif

// Called once by the process loader
static void mpc_process_load(void) {
   mpc_heap_main_init();
#if defined(MPC_TLS_RECURSE_GUARD)
   volatile mpc_heap_t* dummy = _mpc_heap_default; // access TLS to allocate it before setting tls_initialized to true;
   UNUSED(dummy);
#endif
   os_preloading = false;
   atexit(&mpc_process_done);
   _mpc_options_init();
   mpc_process_init();

   if (mpc_redirected) _mpc_verbose_message("malloc is redirected.\n");

   // show message from the redirector (if present)
   const char* msg = NULL;
   mpc_allocator_init(&msg);
   if (msg != NULL && (mpc_option_is_enabled(mpc_option_verbose) || mpc_option_is_enabled(mpc_option_show_errors))) {
      _mpc_fputs(NULL, NULL, NULL, msg);
   }
}

#if defined(_WIN32) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
mpc_decl_cache_align bool _mpc_cpu_has_fsrm = false;

static void mpc_detect_cpu_features(void) {
   // FSRM for fast rep movsb support (AMD Zen3+ (~2020) or Intel Ice Lake+ (~2017))
   int32_t cpu_info[4];
   __cpuid(cpu_info, 7);
   _mpc_cpu_has_fsrm = ((cpu_info[3] & (1 << 4)) != 0); // bit 4 of EDX : see <https ://en.wikipedia.org/wiki/CPUID#EAX=7,_ECX=0:_Extended_Features>
}
#else
static void mpc_detect_cpu_features(void) {
   // nothing
}
#endif

// Initialize the process; called by thread_init or the process loader
void mpc_process_init(void) mpc_attr_noexcept {
   // ensure we are called once
   if (_mpc_process_is_initialized) return;
   _mpc_process_is_initialized = true;
   mpc_process_setup_auto_thread_done();

   _mpc_verbose_message("process init: 0x%zx\n", _mpc_thread_id());
   mpc_detect_cpu_features();
   mpc_heap_main_init();
   mpc_thread_init();
}

// Called when the process is done (through `at_exit`)
static void mpc_process_done(void) {
   // only shutdown if we were initialized
   if (!_mpc_process_is_initialized) return;
   // ensure we are called once
   static bool process_done = false;
   if (process_done) return;
   process_done = true;

#if defined(_WIN32) && !defined(MPC_SHARED_LIB)
   FlsSetValue(mpc_fls_key, NULL);  // don't call main-thread callback
   FlsFree(mpc_fls_key);            // call thread-done on all threads to prevent dangling callback pointer if statically linked with a DLL; Issue #208
#endif

#if !defined(MPC_SHARED_LIB)  
// free all memory if possible on process exit. This is not needed for a stand-alone process
// but should be done if alloc is statically linked into another shared library which
// is repeatedly loaded/unloaded, see issue #281.
   mpc_collect(true /* force */);
#endif

   mpc_allocator_done();
   _mpc_verbose_message("process done: 0x%zx\n", _mpc_heap_main.thread_id);
   os_preloading = true; // don't call the C runtime anymore
}



#if defined(_WIN32) && defined(MPC_SHARED_LIB)
// Windows DLL: easy to hook into process_init and thread_done
__declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
   UNUSED(reserved);
   UNUSED(inst);
   if (reason == DLL_PROCESS_ATTACH) {
      mpc_process_load();
   }
   else if (reason == DLL_THREAD_DETACH) {
      if (!mpc_is_redirected()) mpc_thread_done();
   }
   return TRUE;
}

#else
// C++: use static initialization to detect process start
static bool _mpc_process_init(void) {
   mpc_process_load();
   return (_mpc_heap_main.thread_id != 0);
}
static bool mpc_initialized = _mpc_process_init();
#endif
