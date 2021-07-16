/* ----------------------------------------------------------------------------
Copyright (c) 2018-2021, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_INTERNAL_H
#define MIMALLOC_INTERNAL_H

#include "alloc-types.h"

#define MPC_CACHE_LINE          64
#if defined(_MSC_VER)
#define mpc_decl_noinline        __declspec(noinline)
#define mpc_decl_thread          __declspec(thread)
#define mpc_decl_cache_align     __declspec(align(MPC_CACHE_LINE))
#elif (defined(__GNUC__) && (__GNUC__>=3))  // includes clang and icc
#define mpc_decl_noinline        __attribute__((noinline))
#define mpc_decl_thread          __thread
#define mpc_decl_cache_align     __attribute__((aligned(MPC_CACHE_LINE)))
#else
#define mpc_decl_noinline
#define mpc_decl_thread          __thread        // hope for the best :-)
#define mpc_decl_cache_align
#endif

// "options.c"
void       _mpc_fputs(mpc_output_fun* out, void* arg, const char* prefix, const char* message);
void       _mpc_fprintf(mpc_output_fun* out, void* arg, const char* fmt, ...);
void       _mpc_warning_message(const char* fmt, ...);
void       _mpc_verbose_message(const char* fmt, ...);
void       _mpc_trace_message(const char* fmt, ...);
void       _mpc_options_init(void);
void       _mpc_error_message(int err, const char* fmt, ...);

// init.c
extern mpc_decl_cache_align const mpc_empty_page_s  _mpc_page_empty;
bool       _mpc_is_main_thread(void);
bool       _mpc_preloading();  // true while the C runtime is not ready

// memory.c
void* _mpc_mem_alloc_aligned(size_t size, size_t alignment, bool* commit, bool* large, bool* is_pinned, bool* is_zero, size_t* id, mpc_os_tld_t* tld);
void       _mpc_mem_free(void* p, size_t size, size_t id, bool fully_committed, bool any_reset, mpc_os_tld_t* tld);

bool       _mpc_mem_reset(void* p, size_t size, mpc_os_tld_t* tld);
bool       _mpc_mem_unreset(void* p, size_t size, bool* is_zero, mpc_os_tld_t* tld);
bool       _mpc_mem_commit(void* p, size_t size, bool* is_zero, mpc_os_tld_t* tld);
bool       _mpc_mem_protect(void* addr, size_t size);
bool       _mpc_mem_unprotect(void* addr, size_t size);

// "segment.c"
mpc_page_t* _mpc_segment_page_alloc(mpc_heap_t* heap, size_t block_wsize, mpc_os_tld_t* os_tld);
void       _mpc_segment_page_free(mpc_page_t* page, bool force);
void       _mpc_segment_page_abandon(mpc_page_t* page);
uint8_t* _mpc_segment_page_start(const mpc_page_t* page, size_t block_size, size_t* page_size, size_t* pre_size); // page start for any page
void       _mpc_segment_huge_page_free(mpc_page_t* page, mpc_block_t* block);

void       _mpc_segment_thread_collect();
void       _mpc_abandoned_reclaim_all(mpc_heap_t* heap);
void       _mpc_abandoned_await_readers(void);



// "page.c"
void* _mpc_malloc_generic(mpc_heap_t* heap, size_t size)  mpc_attr_noexcept mpc_attr_malloc;

void       _mpc_page_retire(mpc_page_t* page);                                  // free the page if there are no other pages with many free blocks
void       _mpc_page_unfull(mpc_page_t* page);
void       _mpc_page_free(mpc_page_t* page, mpc_page_queue_t* pq, bool force);   // free the page
void       _mpc_page_abandon(mpc_page_t* page, mpc_page_queue_t* pq);            // abandon the page, to be picked up by another thread...
void       _mpc_heap_delayed_free(mpc_heap_t* heap);
void       _mpc_heap_collect_retired(mpc_heap_t* heap, bool force);

void       _mpc_page_use_delayed_free(mpc_page_t* page, mpc_delayed_t delay, bool override_never);
size_t     _mpc_page_queue_append(mpc_heap_t* heap, mpc_page_queue_t* pq, mpc_page_queue_t* append);
void       _mpc_deferred_free(mpc_heap_t* heap, bool force);

void       _mpc_page_free_collect(mpc_page_t* page, bool force);
void       _mpc_page_reclaim(mpc_heap_t* heap, mpc_page_t* page);   // callback from segments

size_t     _mpc_bin_size(uint8_t bin);           // for stats
uint8_t    _mpc_bin(size_t size);                // for stats

// "heap.c"
void       _mpc_heap_destroy_pages(mpc_heap_t* heap);
void       _mpc_heap_collect_abandon(mpc_heap_t* heap);
void       _mpc_heap_set_default_direct(mpc_heap_t* heap);

mpc_msecs_t  _mpc_clock_now(void);
mpc_msecs_t  _mpc_clock_end(mpc_msecs_t start);
mpc_msecs_t  _mpc_clock_start(void);

// "alloc.c"
void* _mpc_page_malloc(mpc_heap_t* heap, mpc_page_t* page, size_t size) mpc_attr_noexcept;  // called from `_mpc_malloc_generic`
mpc_block_t* _mpc_page_ptr_unalign(const mpc_page_t* page, const void* p);
bool        _mpc_free_delayed_block(mpc_block_t* block);


// ------------------------------------------------------
// Branches
// ------------------------------------------------------

#if defined(__GNUC__) || defined(__clang__)
#define mpc_unlikely(x)     __builtin_expect((x),0)
#define mpc_likely(x)       __builtin_expect((x),1)
#else
#define mpc_unlikely(x)     (x)
#define mpc_likely(x)       (x)
#endif

#ifndef __has_builtin
#define __has_builtin(x)  0
#endif


/* -----------------------------------------------------------
  Error codes passed to `_mpc_fatal_error`
  All are recoverable but EFAULT is a serious error and aborts by default in secure mode.
  For portability define undefined error codes using common Unix codes:
  <https://www-numi.fnal.gov/offline_software/srt_public_context/WebDocs/Errors/unix_system_errors.html>
----------------------------------------------------------- */
#include <errno.h>
#ifndef EAGAIN         // double free
#define EAGAIN (11)
#endif
#ifndef ENOMEM         // out of memory
#define ENOMEM (12)
#endif
#ifndef EFAULT         // corrupted free-list or meta-data
#define EFAULT (14)
#endif
#ifndef EINVAL         // trying to free an invalid pointer
#define EINVAL (22)
#endif
#ifndef EOVERFLOW      // count*size overflow
#define EOVERFLOW (75)
#endif


/* -----------------------------------------------------------
  Inlined definitions
----------------------------------------------------------- */

#define MPC_INIT4(x)   x(),x(),x(),x()
#define MPC_INIT8(x)   MPC_INIT4(x),MPC_INIT4(x)
#define MPC_INIT16(x)  MPC_INIT8(x),MPC_INIT8(x)
#define MPC_INIT32(x)  MPC_INIT16(x),MPC_INIT16(x)
#define MPC_INIT64(x)  MPC_INIT32(x),MPC_INIT32(x)
#define MPC_INIT128(x) MPC_INIT64(x),MPC_INIT64(x)
#define MPC_INIT256(x) MPC_INIT128(x),MPC_INIT128(x)


// Is `x` a power of two? (0 is considered a power of two)
static inline bool _mpc_is_power_of_two(uintptr_t x) {
   return ((x & (x - 1)) == 0);
}

// Align upwards
static inline uintptr_t _mpc_align_up(uintptr_t sz, size_t alignment) {
   uintptr_t mask = alignment - 1;
   if ((alignment & mask) == 0) {  // power of two?
      return ((sz + mask) & ~mask);
   }
   else {
      return (((sz + mask) / alignment) * alignment);
   }
}

// Divide upwards: `s <= _mpc_divide_up(s,d)*d < s+d`.
static inline uintptr_t _mpc_divide_up(uintptr_t size, size_t divider) {
   return (divider == 0 ? size : ((size + divider - 1) / divider));
}

// Is memory zero initialized?
static inline bool mpc_mem_is_zero(void* p, size_t size) {
   for (size_t i = 0; i < size; i++) {
      if (((uint8_t*)p)[i] != 0) return false;
   }
   return true;
}

// Align a byte size to a size in _machine words_,
// i.e. byte size == `wsize*sizeof(void*)`.
static inline size_t _mpc_wsize_from_size(size_t size) {
   return (size + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
}

// Does malloc satisfy the alignment constraints already?
static inline bool mpc_malloc_satisfies_alignment(size_t alignment, size_t size) {
   return (alignment == sizeof(void*) || (alignment == MPC_MAX_ALIGN_SIZE && size > (MPC_MAX_ALIGN_SIZE / 2)));
}

// Overflow detecting multiply
#if __has_builtin(__builtin_umul_overflow) || __GNUC__ >= 5
#include <limits.h>      // UINT_MAX, ULONG_MAX
#if defined(_CLOCK_T)    // for Illumos
#undef _CLOCK_T
#endif
static inline bool mpc_mul_overflow(size_t count, size_t size, size_t* total) {
#if (SIZE_MAX == UINT_MAX)
   return __builtin_umul_overflow(count, size, total);
#elif (SIZE_MAX == ULONG_MAX)
   return __builtin_umull_overflow(count, size, total);
#else
   return __builtin_umulll_overflow(count, size, total);
#endif
}
#else /* __builtin_umul_overflow is unavailable */
static inline bool mpc_mul_overflow(size_t count, size_t size, size_t* total) {
#define MPC_MUL_NO_OVERFLOW ((size_t)1 << (4*sizeof(size_t)))  // sqrt(SIZE_MAX)
   * total = count * size;
   return ((size >= MPC_MUL_NO_OVERFLOW || count >= MPC_MUL_NO_OVERFLOW)
      && size > 0 && (SIZE_MAX / size) < count);
}
#endif

// Safe multiply `count*size` into `total`; return `true` on overflow.
static inline bool mpc_count_size_overflow(size_t count, size_t size, size_t* total) {
   if (count == 1) {  // quick check for the case where count is one (common for C++ allocators)
      *total = size;
      return false;
   }
   else if (mpc_unlikely(mpc_mul_overflow(count, size, total))) {
      _mpc_error_message(EOVERFLOW, "allocation request is too large (%zu * %zu bytes)\n", count, size);
      *total = SIZE_MAX;
      return true;
   }
   else return false;
}


/* ----------------------------------------------------------------------------------------
The thread local default heap: `_mpc_get_default_heap` returns the thread local heap.
On most platforms (Windows, Linux, FreeBSD, NetBSD, etc), this just returns a
__thread local variable (`_mpc_heap_default`). With the initial-exec TLS model this ensures
that the storage will always be available (allocated on the thread stacks).
On some platforms though we cannot use that when overriding `malloc` since the underlying
TLS implementation (or the loader) will call itself `malloc` on a first access and recurse.
We try to circumvent this in an efficient way:
- macOSX : we use an unused TLS slot from the OS allocated slots (MPC_TLS_SLOT). On OSX, the
           loader itself calls `malloc` even before the modules are initialized.
- OpenBSD: we use an unused slot from the pthread block (MPC_TLS_PTHREAD_SLOT_OFS).
- DragonFly: the uniqueid use is buggy but kept for reference.
------------------------------------------------------------------------------------------- */

extern const mpc_heap_t _mpc_heap_empty;  // read-only empty heap, initial value of the thread local default heap
extern bool _mpc_process_is_initialized;
mpc_heap_t* _mpc_heap_main_get(void);    // statically allocated main backing heap

#if defined(MPC_MALLOC_OVERRIDE)
#if defined(__APPLE__) // macOS
#define MPC_TLS_SLOT               89  // seems unused? 
// other possible unused ones are 9, 29, __PTK_FRAMEWORK_JAVASCRIPTCORE_KEY4 (94), __PTK_FRAMEWORK_GC_KEY9 (112) and __PTK_FRAMEWORK_OLDGC_KEY9 (89)
// see <https://github.com/rweichler/substrate/blob/master/include/pthread_machdep.h>
#elif defined(__OpenBSD__)
// use end bytes of a name; goes wrong if anyone uses names > 23 characters (ptrhread specifies 16) 
// see <https://github.com/openbsd/src/blob/master/lib/libc/include/thread_private.h#L371>
#define MPC_TLS_PTHREAD_SLOT_OFS   (6*sizeof(int) + 4*sizeof(void*) + 24)  
#elif defined(__DragonFly__)
#warning "alloc is not working correctly on DragonFly yet."
//#define MPC_TLS_PTHREAD_SLOT_OFS   (4 + 1*sizeof(void*))  // offset `uniqueid` (also used by gdb?) <https://github.com/DragonFlyBSD/DragonFlyBSD/blob/master/lib/libthread_xu/thread/thr_private.h#L458>
#endif
#endif

#if defined(MPC_TLS_SLOT)
static inline void* mpc_tls_slot(size_t slot) mpc_attr_noexcept;   // forward declaration
#elif defined(MPC_TLS_PTHREAD_SLOT_OFS)
#include <pthread.h>
static inline mpc_heap_t** mpc_tls_pthread_heap_slot(void) {
   pthread_t self = pthread_self();
#if defined(__DragonFly__)
   if (self == NULL) {
      mpc_heap_t* pheap_main = _mpc_heap_main_get();
      return &pheap_main;
   }
#endif
   return (mpc_heap_t**)((uint8_t*)self + MPC_TLS_PTHREAD_SLOT_OFS);
}
#elif defined(MPC_TLS_PTHREAD)
#include <pthread.h>
extern pthread_key_t _mpc_heap_default_key;
#endif

// Default heap to allocate from (if not using TLS- or pthread slots).
// Do not use this directly but use through `mpc_heap_get_default()` (or the unchecked `mpc_get_default_heap`).
// This thread local variable is only used when neither MPC_TLS_SLOT, MPC_TLS_PTHREAD, or MPC_TLS_PTHREAD_SLOT_OFS are defined.
// However, on the Apple M1 we do use the address of this variable as the unique thread-id (issue #356).
extern mpc_decl_thread mpc_heap_t* _mpc_heap_default;  // default heap to allocate from

static inline mpc_heap_t* mpc_get_default_heap(void) {
#if defined(MPC_TLS_SLOT)
   mpc_heap_t* heap = (mpc_heap_t*)mpc_tls_slot(MPC_TLS_SLOT);
   return (mpc_unlikely(heap == NULL) ? (mpc_heap_t*)&_mpc_heap_empty : heap);
#elif defined(MPC_TLS_PTHREAD_SLOT_OFS)
   mpc_heap_t* heap = *mpc_tls_pthread_heap_slot();
   return (mpc_unlikely(heap == NULL) ? (mpc_heap_t*)&_mpc_heap_empty : heap);
#elif defined(MPC_TLS_PTHREAD)
   mpc_heap_t* heap = (mpc_unlikely(_mpc_heap_default_key == (pthread_key_t)(-1)) ? _mpc_heap_main_get() : (mpc_heap_t*)pthread_getspecific(_mpc_heap_default_key));
   return (mpc_unlikely(heap == NULL) ? (mpc_heap_t*)&_mpc_heap_empty : heap);
#else
#if defined(MPC_TLS_RECURSE_GUARD)
   if (mpc_unlikely(!_mpc_process_is_initialized)) return _mpc_heap_main_get();
#endif
   return _mpc_heap_default;
#endif
}

static inline bool mpc_heap_is_default(const mpc_heap_t* heap) {
   return (heap == mpc_get_default_heap());
}

static inline bool mpc_heap_is_backing(const mpc_heap_t* heap) {
   return (heap->tld->heap_backing == heap);
}

static inline bool mpc_heap_is_initialized(mpc_heap_t* heap) {
   return (heap != &_mpc_heap_empty);
}

/* -----------------------------------------------------------
  Pages
----------------------------------------------------------- */

static inline mpc_page_t* _mpc_heap_get_free_small_page(mpc_heap_t* heap, size_t size) {
   const size_t idx = _mpc_wsize_from_size(size);
   return heap->pages_free_direct[idx];
}

// Get the page belonging to a certain size class
static inline mpc_page_t* _mpc_get_free_small_page(size_t size) {
   return _mpc_heap_get_free_small_page(mpc_get_default_heap(), size);
}

// Get the page containing the pointer
static inline mpc_page_t* _mpc_segment_page_of(const void* p) {
   auto page = (mpc_page_t*)sat::memory::table[uintptr_t(p) >> sat::memory::cSegmentSizeL2];
   page->getHeapID();
   return page;
}

// Quick page start for initialized pages
static inline uint8_t* _mpc_page_start(const mpc_page_t* page, size_t* page_size) {
   const size_t bsize = page->xblock_size;
   return _mpc_segment_page_start(page, bsize, page_size, NULL);
}

// Get the page containing the pointer
static inline mpc_page_t* _mpc_ptr_page(void* p) {
   return _mpc_segment_page_of(p);
}

// Thread free access
static inline mpc_block_t* mpc_page_thread_free(const mpc_page_t* page) {
   return (mpc_block_t*)(mpc_atomic_load_relaxed(&((mpc_page_t*)page)->xthread_free) & ~3);
}

static inline mpc_delayed_t mpc_page_thread_free_flag(const mpc_page_t* page) {
   return (mpc_delayed_t)(mpc_atomic_load_relaxed(&((mpc_page_t*)page)->xthread_free) & 3);
}

// Heap access
static inline mpc_heap_t* mpc_page_heap(const mpc_page_t* page) {
   return (mpc_heap_t*)(mpc_atomic_load_relaxed(&((mpc_page_t*)page)->xheap));
}

static inline void mpc_page_set_heap(mpc_page_t* page, mpc_heap_t* heap) {
   mpc_atomic_store_release(&page->xheap, (uintptr_t)heap);
}

// Thread free flag helpers
static inline mpc_block_t* mpc_tf_block(mpc_thread_free_t tf) {
   return (mpc_block_t*)(tf & ~0x03);
}
static inline mpc_delayed_t mpc_tf_delayed(mpc_thread_free_t tf) {
   return (mpc_delayed_t)(tf & 0x03);
}
static inline mpc_thread_free_t mpc_tf_make(mpc_block_t* block, mpc_delayed_t delayed) {
   return (mpc_thread_free_t)((uintptr_t)block | (uintptr_t)delayed);
}
static inline mpc_thread_free_t mpc_tf_set_delayed(mpc_thread_free_t tf, mpc_delayed_t delayed) {
   return mpc_tf_make(mpc_tf_block(tf), delayed);
}
static inline mpc_thread_free_t mpc_tf_set_block(mpc_thread_free_t tf, mpc_block_t* block) {
   return mpc_tf_make(block, mpc_tf_delayed(tf));
}

// are all blocks in a page freed?
// note: needs up-to-date used count, (as the `xthread_free` list may not be empty). see `_mpc_page_collect_free`.
static inline bool mpc_page_all_free(const mpc_page_t* page) {
   return (page->used == 0);
}

// are there any available blocks?
static inline bool mpc_page_has_any_available(const mpc_page_t* page) {
   return (page->used < page->reserved || (mpc_page_thread_free(page) != NULL));
}

// are there immediately available blocks, i.e. blocks available on the free list.
static inline bool mpc_page_immediate_available(const mpc_page_t* page) {
   return (page->freelist != NULL);
}

// is more than 7/8th of a page in use?
static inline bool mpc_page_mostly_used(const mpc_page_t* page) {
   if (page == NULL) return true;
   uint16_t frac = page->reserved / 8U;
   return (page->reserved - page->used <= frac);
}

static inline mpc_page_queue_t* mpc_page_queue(const mpc_heap_t* heap, size_t size) {
   return &((mpc_heap_t*)heap)->pages[_mpc_bin(size)];
}



//-----------------------------------------------------------
// Page flags
//-----------------------------------------------------------
static inline bool mpc_page_is_in_full(const mpc_page_t* page) {
   return page->flags.x.in_full;
}

static inline void mpc_page_set_in_full(mpc_page_t* page, bool in_full) {
   page->flags.x.in_full = in_full;
}

static inline bool mpc_page_has_aligned(const mpc_page_t* page) {
   return page->flags.x.has_aligned;
}

static inline void mpc_page_set_has_aligned(mpc_page_t* page, bool has_aligned) {
   page->flags.x.has_aligned = has_aligned;
}


/* -------------------------------------------------------------------
Encoding/Decoding the free list next pointers

This is to protect against buffer overflow exploits where the
free list is mutated. Many hardened allocators xor the next pointer `p`
with a secret key `k1`, as `p^k1`. This prevents overwriting with known
values but might be still too weak: if the attacker can guess
the pointer `p` this  can reveal `k1` (since `p^k1^p == k1`).
Moreover, if multiple blocks can be read as well, the attacker can
xor both as `(p1^k1) ^ (p2^k1) == p1^p2` which may reveal a lot
about the pointers (and subsequently `k1`).

Instead alloc uses an extra key `k2` and encodes as `((p^k2)<<<k1)+k1`.
Since these operations are not associative, the above approaches do not
work so well any more even if the `p` can be guesstimated. For example,
for the read case we can subtract two entries to discard the `+k1` term,
but that leads to `((p1^k2)<<<k1) - ((p2^k2)<<<k1)` at best.
We include the left-rotation since xor and addition are otherwise linear
in the lowest bit. Finally, both keys are unique per page which reduces
the re-use of keys by a large factor.

We also pass a separate `null` value to be used as `NULL` or otherwise
`(k2<<<k1)+k1` would appear (too) often as a sentinel value.
------------------------------------------------------------------- */

static inline bool mpc_is_in_same_page(const void* p, const void* q) {
   return (_mpc_segment_page_of(p) == _mpc_segment_page_of(q));
}

static inline mpc_block_t* mpc_block_nextx(const void* null, const mpc_block_t* block) {
   return (mpc_block_t*)block->next;
}

static inline void mpc_block_set_nextx(const void* null, mpc_block_t* block, const mpc_block_t* next) {
   block->next = (mpc_encoded_t)next;
}

static inline mpc_block_t* mpc_block_next(const mpc_page_t* page, const mpc_block_t* block) {
   return mpc_block_nextx(page, block);
}

static inline void mpc_block_set_next(const mpc_page_t* page, mpc_block_t* block, const mpc_block_t* next) {
   mpc_block_set_nextx(page, block, next);
}

// -------------------------------------------------------------------
// Optimize numa node access for the common case (= one node)
// -------------------------------------------------------------------

int    _mpc_os_numa_node_get(mpc_os_tld_t* tld);
size_t _mpc_os_numa_node_count_get(void);

extern _Atomic(size_t)_mpc_numa_node_count;
static inline int _mpc_os_numa_node(mpc_os_tld_t* tld) {
   if (mpc_likely(mpc_atomic_load_relaxed(&_mpc_numa_node_count) == 1)) return 0;
   else return _mpc_os_numa_node_get(tld);
}
static inline size_t _mpc_os_numa_node_count(void) {
   const size_t count = mpc_atomic_load_relaxed(&_mpc_numa_node_count);
   if (mpc_likely(count > 0)) return count;
   else return _mpc_os_numa_node_count_get();
}


// -------------------------------------------------------------------
// Getting the thread id should be performant as it is called in the
// fast path of `_mpc_free` and we specialize for various platforms.
// -------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static inline uintptr_t _mpc_thread_id(void) mpc_attr_noexcept {
   // Windows: works on Intel and ARM in both 32- and 64-bit
   return (uintptr_t)NtCurrentTeb();
}

// -----------------------------------------------------------------------
// Count bits: trailing or leading zeros (with MPC_INTPTR_BITS on all zero)
// -----------------------------------------------------------------------

#include <limits.h>       // LONG_MAX
#define MPC_HAVE_FAST_BITSCAN
static inline size_t mpc_clz(uintptr_t x) {
   if (x == 0) return MPC_INTPTR_BITS;
   unsigned long idx;
#if (INTPTR_MAX == LONG_MAX)
   _BitScanReverse(&idx, x);
#else
   _BitScanReverse64(&idx, x);
#endif  
   return ((MPC_INTPTR_BITS - 1) - idx);
}
static inline size_t mpc_ctz(uintptr_t x) {
   if (x == 0) return MPC_INTPTR_BITS;
   unsigned long idx;
#if (INTPTR_MAX == LONG_MAX)
   _BitScanForward(&idx, x);
#else
   _BitScanForward64(&idx, x);
#endif  
   return idx;
}

// "bit scan reverse": Return index of the highest bit (or MPC_INTPTR_BITS if `x` is zero)
static inline size_t mpc_bsr(uintptr_t x) {
   return (x == 0 ? MPC_INTPTR_BITS : MPC_INTPTR_BITS - 1 - mpc_clz(x));
}


// ---------------------------------------------------------------------------------
// Provide our own `_mpc_memcpy` for potential performance optimizations.
//
// For now, only on Windows with msvc/clang-cl we optimize to `rep movsb` if 
// we happen to run on x86/x64 cpu's that have "fast short rep movsb" (FSRM) support 
// (AMD Zen3+ (~2020) or Intel Ice Lake+ (~2017). See also issue #201 and pr #253. 
// ---------------------------------------------------------------------------------

#include <intrin.h>
#include <string.h>
extern bool _mpc_cpu_has_fsrm;
static inline void _mpc_memcpy(void* dst, const void* src, size_t n) {
   if (_mpc_cpu_has_fsrm) {
      __movsb((unsigned char*)dst, (const unsigned char*)src, n);
   }
   else {
      memcpy(dst, src, n); // todo: use noinline?
   }
}

// Default fallback on `_mpc_memcpy`
static inline void _mpc_memcpy_aligned(void* dst, const void* src, size_t n) {
   _mpc_memcpy(dst, src, n);
}

#endif