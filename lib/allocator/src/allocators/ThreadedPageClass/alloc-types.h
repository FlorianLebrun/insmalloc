/* ----------------------------------------------------------------------------
Copyright (c) 2018-2021, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_TYPES_H
#define MIMALLOC_TYPES_H

#include <stddef.h>   // ptrdiff_t
#include <stdint.h>   // uintptr_t, uint16_t, etc
#include "alloc-atomic.h"  // _Atomic

#ifdef _MSC_VER
#pragma warning(disable:4214) // bitfield is not int
#endif 

// Minimal alignment necessary. On most platforms 16 bytes are needed
// due to SSE registers for example. This must be at least `MPC_INTPTR_SIZE`
#ifndef MPC_MAX_ALIGN_SIZE
#define MPC_MAX_ALIGN_SIZE  16   // sizeof(max_align_t)
#endif

// ------------------------------------------------------
// Platform specific values
// ------------------------------------------------------

// ------------------------------------------------------
// Size of a pointer.
// We assume that `sizeof(void*)==sizeof(intptr_t)`
// and it holds for all platforms we know of.
//
// However, the C standard only requires that:
//  p == (void*)((intptr_t)p))
// but we also need:
//  i == (intptr_t)((void*)i)
// or otherwise one might define an intptr_t type that is larger than a pointer...
// ------------------------------------------------------

# define MPC_INTPTR_L2 (3)

#define MPC_INTPTR_SIZE  (1<<MPC_INTPTR_L2)
#define MPC_INTPTR_BITS  (MPC_INTPTR_SIZE*8)

#define KiB     ((size_t)1024)
#define MiB     (KiB*KiB)
#define GiB     (MiB*KiB)


// ------------------------------------------------------
// Main internal data-structures
// ------------------------------------------------------

// Main tuning parameters for segment and page sizes
// Sizes for 64-bit, divide by two for 32-bit
#define MPC_SMALL_PAGE_L2               (16)      // 64kb
#define MPC_MEDIUM_PAGE_L2              ( 3 + MPC_SMALL_PAGE_L2)  // 512kb
#define MPC_LARGE_PAGE_L2               ( 3 + MPC_MEDIUM_PAGE_L2) // 4mb

// Derived constants
#define MPC_SMALL_PAGE_SIZE                (1UL<<MPC_SMALL_PAGE_L2)
#define MPC_MEDIUM_PAGE_SIZE               (1UL<<MPC_MEDIUM_PAGE_L2)
#define MPC_LARGE_PAGE_SIZE                (1UL<<MPC_LARGE_PAGE_L2)

// The max object size are checked to not waste more than 12.5% internally over the page sizes.
// (Except for large pages since huge objects are allocated in 4MiB chunks)
#define MPC_SMALL_OBJ_SIZE_MAX             (MPC_SMALL_PAGE_SIZE/4)   // 16kb
#define MPC_MEDIUM_OBJ_SIZE_MAX            (MPC_MEDIUM_PAGE_SIZE/4)  // 128kb
#define MPC_LARGE_OBJ_SIZE_MAX             (MPC_LARGE_PAGE_SIZE/2)   // 2mb
#define MPC_LARGE_OBJ_WSIZE_MAX            (MPC_LARGE_OBJ_SIZE_MAX/MPC_INTPTR_SIZE)

// Maximum number of size classes. (spaced exponentially in 12.5% increments)
#define MPC_BIN_HUGE  (73U)

#if (MPC_LARGE_OBJ_WSIZE_MAX >= 655360)
#error "define more bins"
#endif

// The free lists use encoded next fields
// (Only actually encodes when MPC_ENCODED_FREELIST is defined.)
typedef uintptr_t mpc_encoded_t;

// free lists contain blocks
typedef struct mpc_block_s {
   mpc_encoded_t next;
} mpc_block_t;


// The delayed flags are used for efficient multi-threaded free-ing
typedef enum mpc_delayed_e {
   MPC_USE_DELAYED_FREE = 0, // push on the owning heap thread delayed list
   MPC_DELAYED_FREEING = 1, // temporary: another thread is accessing the owning heap
   MPC_NO_DELAYED_FREE = 2, // optimize: push on page local thread free queue if another block is already in the heap thread delayed free list
   MPC_NEVER_DELAYED_FREE = 3  // sticky, only resets on page reclaim
} mpc_delayed_t;


// The `in_full` and `has_aligned` page flags are put in a union to efficiently
// test if both are false (`full_aligned == 0`) in the `mpc_free` routine.
#if !MPC_TSAN
typedef union mpc_page_flags_s {
   uint8_t full_aligned;
   struct {
      uint8_t in_full : 1;
      uint8_t has_aligned : 1;
   } x;
} mpc_page_flags_t;
#else
// under thread sanitizer, use a byte for each flag to suppress warning, issue #130
typedef union mpc_page_flags_s {
   uint16_t full_aligned;
   struct {
      uint8_t in_full;
      uint8_t has_aligned;
   } x;
} mpc_page_flags_t;
#endif

// Thread free list.
// We use the bottom 2 bits of the pointer for mpc_delayed_t flags
typedef uintptr_t mpc_thread_free_t;


typedef enum mpc_page_kind_e {
   MPC_PAGE_EMPTY = 0,
   MPC_PAGE_SMALL,    // small blocks go into 64kb pages inside a segment
   MPC_PAGE_MEDIUM,   // medium blocks go into 512kb pages inside a segment
   MPC_PAGE_LARGE,    // larger blocks go into a single page spanning a whole segment
   MPC_PAGE_HUGE,     // huge blocks (>512kb) are put into a single page in a segment of the exact size (but still 2mb aligned)
} mpc_page_kind_t;


// A page contains blocks of one specific size (`block_size`).
// Each page has three list of free blocks:
// `free` for blocks that can be allocated,
// `local_free` for freed blocks that are not yet available to `mpc_malloc`
// `thread_free` for freed blocks by other threads
// The `local_free` and `thread_free` lists are migrated to the `free` list
// when it is exhausted. The separate `local_free` list is necessary to
// implement a monotonic heartbeat. The `thread_free` list is needed for
// avoiding atomic operations in the common case.
//
//
// `used - |thread_free|` == actual blocks that are in use (alive)
// `used - |thread_free| + |free| + |local_free| == capacity`
//
// We don't count `freed` (as |free|) but use `used` to reduce
// the number of memory accesses in the `mpc_page_all_free` function(s).
//
// Notes: 
// - Access is optimized for `mpc_free` and `mpc_page_alloc` (in `alloc.c`)
// - Using `uint16_t` does not seem to slow things down
// - The size is 8 words on 64-bit which helps the page index calculations
//   (and 10 words on 32-bit, and encoded free lists add 2 words. Sizes 10 
//    and 12 are still good for address calculation)
// - To limit the structure size, the `xblock_size` is 32-bits only; for 
//   blocks > MPC_HUGE_BLOCK_SIZE the size is determined from the segment page size
// - `thread_free` uses the bottom bits as a delayed-free flags to optimize
//   concurrent frees where only the first concurrent free adds to the owning
//   heap `thread_delayed_free` list (see `alloc.c:mpc_free_block_mt`).
//   The invariant is that no-delayed-free is only set if there is
//   at least one block that will be added, or as already been added, to 
//   the owning heap `thread_delayed_free` list. This guarantees that pages
//   will be freed correctly even if only other threads free blocks.
typedef struct mpc_page_s : public sat::MemorySegmentController {

   _Atomic(uintptr_t)thread_id = 0;        // unique id of the thread owning this segment
   mpc_page_kind_t       page_kind = MPC_PAGE_EMPTY;        // kind of pages: small, large, or huge
   uint8_t               page_in_use : 1;  // `true` if the segment allocated this page

   uint8_t               is_reset : 1;        // `true` if the page memory was reset
   uint8_t               is_committed : 1;    // `true` if the page virtual memory is committed
   uint8_t               is_zero_init : 1;    // `true` if the page was zero initialized

   // layout like this to optimize access in `mpc_malloc` and `mpc_free`
   uint16_t              capacity = 0;          // number of blocks committed, must be the first field, see `segment.c:page_clear`
   uint16_t              reserved = 0;          // number of blocks reserved in memory
   mpc_page_flags_t      flags = { 0 };             // `in_full` and `has_aligned` flags (8 bits)
   uint8_t               is_zero : 1;         // `true` if the blocks in the free list are zero initialized
   uint8_t               retire_expire : 7;   // expiration count for retired blocks

   mpc_block_t*          freelist = 0;              // list of available free blocks (`malloc` allocates from this list)
   uint32_t              used = 0;              // number of blocks in use (including blocks in `local_free` and `thread_free`)
   uint32_t              xblock_size = 0;       // size available in each block (always `>0`) 

   mpc_block_t* local_free = 0;        // list of deferred free blocks by this thread (migrates to `free`)
   _Atomic(mpc_thread_free_t)xthread_free = 0;  // list of deferred free blocks freed by other threads
   _Atomic(uintptr_t)xheap = 0;

   struct mpc_page_s* next = 0;              // next page owned by this thread with the same `block_size`
   struct mpc_page_s* prev = 0;              // previous page owned by this thread with the same `block_size`

   mpc_page_s()
      : page_in_use(false), is_reset(false), is_committed(false), is_zero_init(false), is_zero(false), retire_expire(0) {
   }

   virtual size_t getPageSize() const = 0;
   virtual void disposePage() = 0;
   virtual int free(uintptr_t index, uintptr_t ptr) override;

} mpc_page_t;

struct mpc_empty_page_s : public mpc_page_s {
   virtual const char* getName() override { return "MI-EMPTY"; };
   virtual size_t getPageSize() const override { return 0; };
   virtual void disposePage() { throw; }
};


// ------------------------------------------------------
// Heaps
// Provide first-class heaps to allocate from.
// A heap just owns a set of pages for allocation and
// can only be allocate/reallocate from the thread that created it.
// Freeing blocks can be done from any thread though.
// Per thread, the segments are shared among its heaps.
// Per thread, there is always a default heap that is
// used for allocation; it is initialized to statically
// point to an empty heap to avoid initialization checks
// in the fast path.
// ------------------------------------------------------

// Thread local data
typedef struct mpc_tld_s mpc_tld_t;

// Pages of a certain block size are held in a queue.
typedef struct mpc_page_queue_s {
   mpc_page_t* first;
   mpc_page_t* last;
   size_t     block_size;
} mpc_page_queue_t;

#define MPC_BIN_FULL  (MPC_BIN_HUGE+1)

#define MPC_PAGES_DIRECT   (MPC_SMALL_WSIZE_MAX + 1)


// A heap owns a set of pages.
struct mpc_heap_s {
   mpc_tld_t* tld;
   mpc_page_t* pages_free_direct[MPC_PAGES_DIRECT];  // optimize: array where every entry points a page with possibly free blocks in the corresponding queue for that size.
   mpc_page_queue_t       pages[MPC_BIN_FULL + 1];              // queue of pages for each size class (or "bin")
   _Atomic(mpc_block_t*)thread_delayed_free;
   uintptr_t             thread_id;                           // thread this heap belongs too
   size_t                page_count;                          // total number of pages in the `pages` queues.
   size_t                page_retired_min;                    // smallest retired index (retired pages are fully free, but still in the page queues)
   size_t                page_retired_max;                    // largest retired index into the `pages` array.
   mpc_heap_t* next;                                // list of heaps per thread
   bool                  no_reclaim;                          // `true` if this heap should not reclaim abandoned pages
};

// ------------------------------------------------------
// Thread Local data
// ------------------------------------------------------

typedef int64_t  mpc_msecs_t;

// OS thread local data
typedef struct mpc_os_tld_s {
   size_t                region_idx;   // start point for next allocation
} mpc_os_tld_t;

// Thread local data
struct mpc_tld_s {
   unsigned long long  heartbeat;     // monotonic heartbeat count
   bool                recurse;       // true if deferred was called; used to prevent infinite recursion.
   mpc_heap_t* heap_backing;  // backing heap of this thread (cannot be deleted)
   mpc_heap_t* heaps;         // list of heaps in this thread (so we can abandon all when the thread terminates)
   mpc_os_tld_t         os;            // os tld
};

#endif
