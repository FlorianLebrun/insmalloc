/* ----------------------------------------------------------------------------
Copyright (c) 2018-2021, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "alloc.h"
#include "alloc-internal.h"
#include "alloc-atomic.h"

#include <string.h>  // memset, strlen
#include <stdlib.h>  // malloc, exit


// ------------------------------------------------------
// Allocation
// ------------------------------------------------------

// Fast allocation in a page: just pop from the free list.
// Fall back to generic allocation only if the list is empty.
extern inline void* _mpc_page_malloc(mpc_heap_t* heap, mpc_page_t* page, size_t size) mpc_attr_noexcept {
   mpc_block_t* const block = page->freelist;
   if (mpc_unlikely(block == NULL)) {
      return _mpc_malloc_generic(heap, size);
   }
   // pop from the free list
   page->used++;
   page->freelist = mpc_block_next(page, block);
   page->getHeapID();
   return block;
}

// allocate a small block
extern inline mpc_decl_restrict void* mpc_heap_malloc_small(mpc_heap_t* heap, size_t size) mpc_attr_noexcept {
   _ASSERT(heap->thread_id == 0 || heap->thread_id == _mpc_thread_id()); // heaps are thread local

   mpc_page_t* page = _mpc_heap_get_free_small_page(heap, size);
   void* p = _mpc_page_malloc(heap, page, size);

   return p;
}

// The main allocation function
extern inline mpc_decl_restrict void* mpc_heap_malloc(mpc_heap_t* heap, size_t size) mpc_attr_noexcept {
   if (mpc_likely(size <= MPC_SMALL_SIZE_MAX)) {
      return mpc_heap_malloc_small(heap, size);
   }
   else {
      _ASSERT(heap->thread_id == 0 || heap->thread_id == _mpc_thread_id()); // heaps are thread local
      return _mpc_malloc_generic(heap, size);      // note: size can overflow but it is detected in malloc_generic
   }
}

void* mpc_malloc(size_t size) {
   return mpc_heap_malloc(mpc_get_default_heap(), size);
}

// ------------------------------------------------------
// Check for double free in secure and debug mode
// This is somewhat expensive so only enabled for secure mode 4
// ------------------------------------------------------

static inline bool mpc_check_is_double_free(const mpc_page_t* page, const mpc_block_t* block) {
   return false;
}

// ---------------------------------------------------------------------------
// Check for heap block overflow by setting up padding at the end of the block
// ---------------------------------------------------------------------------

static void mpc_check_padding(const mpc_page_t* page, const mpc_block_t* block) {
}

static void mpc_padding_shrink(const mpc_page_t* page, const mpc_block_t* block, const size_t min_size) {
}

// only maintain stats for smaller objects if requested
static void mpc_stat_free(const mpc_page_t* page, const mpc_block_t* block) {
}

static void mpc_stat_huge_free(const mpc_page_t* page) {
}

// ------------------------------------------------------
// Free
// ------------------------------------------------------

// multi-threaded free
static mpc_decl_noinline void _mpc_free_block_mt(mpc_page_t* page, mpc_block_t* block)
{
   // The padding check may access the non-thread-owned page for the key values.
   // that is safe as these are constant and the page won't be freed (as the block is not freed yet).
   mpc_check_padding(page, block);
   mpc_padding_shrink(page, block, sizeof(mpc_block_t)); // for small size, ensure we can fit the delayed thread pointers without triggering overflow detection

   // Try to put the block on either the page-local thread free list, or the heap delayed free list.
   mpc_thread_free_t tfreex;
   bool use_delayed;
   mpc_thread_free_t tfree = mpc_atomic_load_relaxed(&page->xthread_free);
   do {
      use_delayed = (mpc_tf_delayed(tfree) == MPC_USE_DELAYED_FREE);
      if (mpc_unlikely(use_delayed)) {
         // unlikely: this only happens on the first concurrent free in a page that is in the full list
         tfreex = mpc_tf_set_delayed(tfree, MPC_DELAYED_FREEING);
      }
      else {
         // usual: directly add to page thread_free list
         mpc_block_set_next(page, block, mpc_tf_block(tfree));
         tfreex = mpc_tf_set_block(tfree, block);
      }
   } while (!mpc_atomic_cas_weak_release(&page->xthread_free, &tfree, tfreex));

   if (mpc_unlikely(use_delayed)) {
      // racy read on `heap`, but ok because MPC_DELAYED_FREEING is set (see `mpc_heap_delete` and `mpc_heap_collect_abandon`)
      mpc_heap_t* const heap = (mpc_heap_t*)(mpc_atomic_load_acquire(&page->xheap));
      if (heap != NULL) {
         // add to the delayed free list of this heap. (do this atomically as the lock only protects heap memory validity)
         mpc_block_t* dfree = mpc_atomic_load_ptr_relaxed(mpc_block_t, &heap->thread_delayed_free);
         do {
            mpc_block_set_nextx(heap, block, dfree);
         } while (!mpc_atomic_cas_ptr_weak_release(mpc_block_t, &heap->thread_delayed_free, &dfree, block));
      }

      // and reset the MPC_DELAYED_FREEING flag
      tfree = mpc_atomic_load_relaxed(&page->xthread_free);
      do {
         tfreex = tfree;
         tfreex = mpc_tf_set_delayed(tfree, MPC_NO_DELAYED_FREE);
      } while (!mpc_atomic_cas_weak_release(&page->xthread_free, &tfree, tfreex));
   }
}

// regular free
static inline void _mpc_free_block(mpc_page_t* page, bool local, mpc_block_t* block)
{
   // and push it on the free list
   if (mpc_likely(local)) {
      // owning thread can free a block directly
      if (mpc_unlikely(mpc_check_is_double_free(page, block))) return;
      mpc_check_padding(page, block);
      mpc_block_set_next(page, block, page->local_free);
      page->local_free = block;
      page->used--;
      if (mpc_unlikely(mpc_page_all_free(page))) {
         _mpc_page_retire(page);
      }
      else if (mpc_unlikely(mpc_page_is_in_full(page))) {
         _mpc_page_unfull(page);
      }
   }
   else {
      _mpc_free_block_mt(page, block);
   }
}


// Adjust a block that was allocated aligned, to the actual start of the block in the page.
mpc_block_t* _mpc_page_ptr_unalign(const mpc_page_t* page, const void* p) {
   const size_t diff = (uint8_t*)p - _mpc_page_start(page, NULL);
   const size_t adjust = (diff % page->xblock_size);
   return (mpc_block_t*)((uintptr_t)p - adjust);
}

static void mpc_decl_noinline mpc_free_generic(bool local, void* p) {
   mpc_page_t* const page = _mpc_segment_page_of(p);
   mpc_block_t* const block = (mpc_page_has_aligned(page) ? _mpc_page_ptr_unalign(page, p) : (mpc_block_t*)p);
   mpc_stat_free(page, block);
   _mpc_free_block(page, local, block);
}

// Free a block
int mpc_page_s::free(uintptr_t index, uintptr_t ptr)
{
   void* p = (void*)ptr;
   const uintptr_t tid = _mpc_thread_id();
   mpc_page_t* const page = _mpc_segment_page_of(p);
   mpc_block_t* const block = (mpc_block_t*)p;

   if (mpc_likely(tid == page->thread_id && page->flags.full_aligned == 0)) {  // the thread id matches and it is not a full page, nor has aligned blocks
     // local, and not full or aligned
      if (mpc_unlikely(mpc_check_is_double_free(page, block))) return 0;
      mpc_check_padding(page, block);
      mpc_stat_free(page, block);
      mpc_block_set_next(page, block, page->local_free);
      page->local_free = block;
      if (mpc_unlikely(--page->used == 0)) {   // using this expression generates better code than: page->used--; if (mpc_page_all_free(page))    
         _mpc_page_retire(page);
      }
   }
   else {
      // non-local, aligned blocks, or a full page; use the more generic path
      // note: recalc page in generic to improve code generation
      mpc_free_generic(tid == page->thread_id, p);
   }
   return 0;
}

bool _mpc_free_delayed_block(mpc_block_t* block) {
   // get segment and page
   mpc_page_t* const page = _mpc_segment_page_of(block);

   // Clear the no-delayed flag so delayed freeing is used again for this page.
   // This must be done before collecting the free lists on this page -- otherwise
   // some blocks may end up in the page `thread_free` list with no blocks in the
   // heap `thread_delayed_free` list which may cause the page to be never freed!
   // (it would only be freed if we happen to scan it in `mpc_page_queue_find_free_ex`)
   _mpc_page_use_delayed_free(page, MPC_USE_DELAYED_FREE, false /* dont overwrite never delayed */);

   // collect all other non-local frees to ensure up-to-date `used` count
   _mpc_page_free_collect(page, false);

   // and free the block (possibly freeing the page as well since used is updated)
   _mpc_free_block(page, true, block);
   return true;
}
