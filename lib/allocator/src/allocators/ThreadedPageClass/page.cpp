/*----------------------------------------------------------------------------
Copyright (c) 2018-2020, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* -----------------------------------------------------------
  The core of the allocator. Every segment contains
  pages of a {certain block size. The main function
  exported is `mpc_malloc_generic`.
----------------------------------------------------------- */

#include "alloc.h"
#include "alloc-internal.h"
#include "alloc-atomic.h"

/* -----------------------------------------------------------
  Definition of page queues for each block size
----------------------------------------------------------- */

#define MPC_IN_PAGE_C
#include "page-queue.h"
#undef MPC_IN_PAGE_C


/* -----------------------------------------------------------
  Page helpers
----------------------------------------------------------- */

// Index a block in a page
static inline mpc_block_t* mpc_page_block_at(const mpc_page_t* page, void* page_start, size_t block_size, size_t i) {
   return (mpc_block_t*)((uint8_t*)page_start + (i * block_size));
}

static void mpc_page_init(mpc_heap_t* heap, mpc_page_t* page, size_t size, mpc_tld_t* tld);
static void mpc_page_extend_free(mpc_heap_t* heap, mpc_page_t* page, mpc_tld_t* tld);

void _mpc_page_use_delayed_free(mpc_page_t* page, mpc_delayed_t delay, bool override_never) {
   mpc_thread_free_t tfreex;
   mpc_delayed_t     old_delay;
   mpc_thread_free_t tfree;
   do {
      tfree = mpc_atomic_load_acquire(&page->xthread_free); // note: must acquire as we can break/repeat this loop and not do a CAS;
      tfreex = mpc_tf_set_delayed(tfree, delay);
      old_delay = mpc_tf_delayed(tfree);
      if (mpc_unlikely(old_delay == MPC_DELAYED_FREEING)) {
         std::this_thread::yield(); // delay until outstanding MPC_DELAYED_FREEING are done.
      }
      else if (delay == old_delay) {
         break; // avoid atomic operation if already equal
      }
      else if (!override_never && old_delay == MPC_NEVER_DELAYED_FREE) {
         break; // leave never-delayed flag set
      }
   } while ((old_delay == MPC_DELAYED_FREEING) ||
      !mpc_atomic_cas_weak_release(&page->xthread_free, &tfree, tfreex));
}

/* -----------------------------------------------------------
  Page collect the `local_free` and `thread_free` lists
----------------------------------------------------------- */

// Collect the local `thread_free` list using an atomic exchange.
// Note: The exchange must be done atomically as this is used right after
// moving to the full list in `mpc_page_collect_ex` and we need to
// ensure that there was no race where the page became unfull just before the move.
static void _mpc_page_thread_free_collect(mpc_page_t* page)
{
   mpc_block_t* head;
   mpc_thread_free_t tfreex;
   mpc_thread_free_t tfree = mpc_atomic_load_relaxed(&page->xthread_free);
   do {
      head = mpc_tf_block(tfree);
      tfreex = mpc_tf_set_block(tfree, NULL);
   } while (!mpc_atomic_cas_weak_acq_rel(&page->xthread_free, &tfree, tfreex));

   // return if the list is empty
   if (head == NULL) return;

   // find the tail -- also to get a proper count (without data races)
   uint32_t max_count = page->capacity; // cannot collect more than capacity
   uint32_t count = 1;
   mpc_block_t* tail = head;
   mpc_block_t* next;
   while ((next = mpc_block_next(page, tail)) != NULL && count <= max_count) {
      count++;
      tail = next;
   }
   // if `count > max_count` there was a memory corruption (possibly infinite list due to double multi-threaded free)
   if (count > max_count) {
      _mpc_error_message(EFAULT, "corrupted thread-free list\n");
      return; // the thread-free items cannot be freed
   }

   // and append the current local free list
   mpc_block_set_next(page, tail, page->local_free);
   page->local_free = head;

   // update counts now
   page->used -= count;
}

void _mpc_page_free_collect(mpc_page_t* page, bool force) {

   // collect the thread free list
   if (force || mpc_page_thread_free(page) != NULL) {  // quick test to avoid an atomic operation
      _mpc_page_thread_free_collect(page);
   }

   // and the local free list
   if (page->local_free != NULL) {
      if (mpc_likely(page->freelist == NULL)) {
         // usual case
         page->freelist = page->local_free;
         page->local_free = NULL;
         page->is_zero = false;
      }
      else if (force) {
         // append -- only on shutdown (force) as this is a linear operation
         mpc_block_t* tail = page->local_free;
         mpc_block_t* next;
         while ((next = mpc_block_next(page, tail)) != NULL) {
            tail = next;
         }
         mpc_block_set_next(page, tail, page->freelist);
         page->freelist = page->local_free;
         page->local_free = NULL;
         page->is_zero = false;
      }
   }

}



/* -----------------------------------------------------------
  Page fresh and retire
----------------------------------------------------------- */

// called from segments when reclaiming abandoned pages
void _mpc_page_reclaim(mpc_heap_t* heap, mpc_page_t* page) {
   // TODO: push on full queue immediately if it is full?
   mpc_page_queue_t* pq = mpc_page_queue(heap, page->xblock_size);
   mpc_page_queue_push(heap, pq, page);
}

// allocate a fresh page from a segment
static mpc_page_t* mpc_page_fresh_alloc(mpc_heap_t* heap, mpc_page_queue_t* pq, size_t block_size) {
   mpc_page_t* page = _mpc_segment_page_alloc(heap, block_size, &heap->tld->os);
   if (page == NULL) {
      // this may be out-of-memory, or an abandoned page was reclaimed (and in our queue)
      return NULL;
   }
   // a fresh page was found, initialize it
   mpc_page_init(heap, page, block_size, heap->tld);
   if (pq != NULL) mpc_page_queue_push(heap, pq, page); // huge pages use pq==NULL
   return page;
}

// Get a fresh page to use
static mpc_page_t* mpc_page_fresh(mpc_heap_t* heap, mpc_page_queue_t* pq) {
   mpc_page_t* page = mpc_page_fresh_alloc(heap, pq, pq->block_size);
   if (page == NULL) return NULL;
   return page;
}

/* -----------------------------------------------------------
   Do any delayed frees
   (put there by other threads if they deallocated in a full page)
----------------------------------------------------------- */
void _mpc_heap_delayed_free(mpc_heap_t* heap) {
   // take over the list (note: no atomic exchange since it is often NULL)
   mpc_block_t* block = mpc_atomic_load_ptr_relaxed(mpc_block_t, &heap->thread_delayed_free);
   while (block != NULL && !mpc_atomic_cas_ptr_weak_acq_rel(mpc_block_t, &heap->thread_delayed_free, &block, NULL)) { /* nothing */ };

   // and free them all
   while (block != NULL) {
      mpc_block_t* next = mpc_block_nextx(heap, block);
      // use internal free instead of regular one to keep stats etc correct
      if (!_mpc_free_delayed_block(block)) {
         // we might already start delayed freeing while another thread has not yet
         // reset the delayed_freeing flag; in that case delay it further by reinserting.
         mpc_block_t* dfree = mpc_atomic_load_ptr_relaxed(mpc_block_t, &heap->thread_delayed_free);
         do {
            mpc_block_set_nextx(heap, block, dfree);
         } while (!mpc_atomic_cas_ptr_weak_release(mpc_block_t, &heap->thread_delayed_free, &dfree, block));
      }
      block = next;
   }
}

/* -----------------------------------------------------------
  Unfull, abandon, free and retire
----------------------------------------------------------- */

// Move a page from the full list back to a regular list
void _mpc_page_unfull(mpc_page_t* page) {
   if (!mpc_page_is_in_full(page)) return;

   mpc_heap_t* heap = mpc_page_heap(page);
   mpc_page_queue_t* pqfull = &heap->pages[MPC_BIN_FULL];
   mpc_page_set_in_full(page, false); // to get the right queue
   mpc_page_queue_t* pq = mpc_heap_page_queue_of(heap, page);
   mpc_page_set_in_full(page, true);
   mpc_page_queue_enqueue_from(pq, pqfull, page);
}

static void mpc_page_to_full(mpc_page_t* page, mpc_page_queue_t* pq) {

   if (mpc_page_is_in_full(page)) return;
   mpc_page_queue_enqueue_from(&mpc_page_heap(page)->pages[MPC_BIN_FULL], pq, page);
   _mpc_page_free_collect(page, false);  // try to collect right away in case another thread freed just before MPC_USE_DELAYED_FREE was set
}


// Abandon a page with used blocks at the end of a thread.
// Note: only call if it is ensured that no references exist from
// the `page->heap->thread_delayed_free` into this page.
// Currently only called through `mpc_heap_collect_ex` which ensures this.
void _mpc_page_abandon(mpc_page_t* page, mpc_page_queue_t* pq) {

   mpc_heap_t* pheap = mpc_page_heap(page);

   // remove from our page list
   mpc_page_queue_remove(pq, page);

   // page is no longer associated with our heap
   mpc_page_set_heap(page, NULL);

}


// Free a page with no more free blocks
void _mpc_page_free(mpc_page_t* page, mpc_page_queue_t* pq, bool force) {

   // no more aligned blocks in here
   mpc_page_set_has_aligned(page, false);

   // remove from the page list
   // (no need to do _mpc_heap_delayed_free first as all blocks are already free)
   mpc_page_queue_remove(pq, page);

   // and free it
   mpc_page_set_heap(page, NULL);
   _mpc_segment_page_free(page, force);
}

#define MPC_MAX_RETIRE_SIZE    MPC_LARGE_OBJ_SIZE_MAX  
#define MPC_RETIRE_CYCLES      (8)

// Retire a page with no more used blocks
// Important to not retire too quickly though as new
// allocations might coming.
// Note: called from `mpc_free` and benchmarks often
// trigger this due to freeing everything and then
// allocating again so careful when changing this.
void _mpc_page_retire(mpc_page_t* page) {

   mpc_page_set_has_aligned(page, false);

   // don't retire too often..
   // (or we end up retiring and re-allocating most of the time)
   // NOTE: refine this more: we should not retire if this
   // is the only page left with free blocks. It is not clear
   // how to check this efficiently though...
   // for now, we don't retire if it is the only page left of this size class.
   mpc_page_queue_t* pq = mpc_page_queue_of(page);
   if (mpc_likely(page->xblock_size <= MPC_MAX_RETIRE_SIZE && !mpc_page_is_in_full(page))) {
      if (pq->last == page && pq->first == page) { // the only page in the queue?
         page->retire_expire = (page->xblock_size <= MPC_SMALL_OBJ_SIZE_MAX ? MPC_RETIRE_CYCLES : MPC_RETIRE_CYCLES / 4);
         mpc_heap_t* heap = mpc_page_heap(page);
         const size_t index = pq - heap->pages;
         if (index < heap->page_retired_min) heap->page_retired_min = index;
         if (index > heap->page_retired_max) heap->page_retired_max = index;
         return; // dont't free after all
      }
   }

   _mpc_page_free(page, pq, false);
}

// free retired pages: we don't need to look at the entire queues
// since we only retire pages that are at the head position in a queue.
void _mpc_heap_collect_retired(mpc_heap_t* heap, bool force) {
   size_t min = MPC_BIN_FULL;
   size_t max = 0;
   for (size_t bin = heap->page_retired_min; bin <= heap->page_retired_max; bin++) {
      mpc_page_queue_t* pq = &heap->pages[bin];
      mpc_page_t* page = pq->first;
      if (page != NULL && page->retire_expire != 0) {
         if (mpc_page_all_free(page)) {
            page->retire_expire--;
            if (force || page->retire_expire == 0) {
               _mpc_page_free(pq->first, pq, force);
            }
            else {
               // keep retired, update min/max
               if (bin < min) min = bin;
               if (bin > max) max = bin;
            }
         }
         else {
            page->retire_expire = 0;
         }
      }
   }
   heap->page_retired_min = min;
   heap->page_retired_max = max;
}


/* -----------------------------------------------------------
  Initialize the initial free list in a page.
----------------------------------------------------------- */

static mpc_decl_noinline void mpc_page_free_list_extend(mpc_page_t* const page, const size_t bsize, const size_t extend)
{
   void* const page_area = _mpc_page_start(page, NULL);

   mpc_block_t* const start = mpc_page_block_at(page, page_area, bsize, page->capacity);

   // initialize a sequential free list
   mpc_block_t* const last = mpc_page_block_at(page, page_area, bsize, page->capacity + extend - 1);
   mpc_block_t* block = start;
   while (block <= last) {
      mpc_block_t* next = (mpc_block_t*)((uint8_t*)block + bsize);
      mpc_block_set_next(page, block, next);
      block = next;
   }
   // prepend to free list (usually `NULL`)
   mpc_block_set_next(page, last, page->freelist);
   page->freelist = start;
}

/* -----------------------------------------------------------
  Page initialize and extend the capacity
----------------------------------------------------------- */

#define MPC_MAX_EXTEND_SIZE    (4*1024)      // heuristic, one OS page seems to work well.
#define MPC_MIN_EXTEND         (1)

// Extend the capacity (up to reserved) by initializing a free list
// We do at most `MPC_MAX_EXTEND` to avoid touching too much memory
// Note: we also experimented with "bump" allocation on the first
// allocations but this did not speed up any benchmark (due to an
// extra test in malloc? or cache effects?)
static void mpc_page_extend_free(mpc_heap_t* heap, mpc_page_t* page, mpc_tld_t* tld) {
   if (page->freelist != NULL) return;
   if (page->capacity >= page->reserved) return;

   size_t page_size;
   //uint8_t* page_start = 
   _mpc_page_start(page, &page_size);

   // calculate the extend count
   const size_t bsize = page->xblock_size;
   size_t extend = page->reserved - page->capacity;
   size_t max_extend = (bsize >= MPC_MAX_EXTEND_SIZE ? MPC_MIN_EXTEND : MPC_MAX_EXTEND_SIZE / (uint32_t)bsize);
   if (max_extend < MPC_MIN_EXTEND) max_extend = MPC_MIN_EXTEND;

   if (extend > max_extend) {
      // ensure we don't touch memory beyond the page to reduce page commit.
      // the `lean` benchmark tests this. Going from 1 to 8 increases rss by 50%.
      extend = (max_extend == 0 ? 1 : max_extend);
   }


   // and append the extend the free list
   mpc_page_free_list_extend(page, bsize, extend);

   // enable the new free list
   page->capacity += (uint16_t)extend;

   // extension into zero initialized memory preserves the zero'd free list
   if (!page->is_zero_init) {
      page->is_zero = false;
   }
}

// Initialize a fresh page
static void mpc_page_init(mpc_heap_t* heap, mpc_page_t* page, size_t block_size, mpc_tld_t* tld) {
   // set fields
   mpc_page_set_heap(page, heap);
   size_t page_size;
   _mpc_segment_page_start(page, block_size, &page_size, NULL);
   page->xblock_size = (uint32_t)block_size;
   page->reserved = (uint16_t)(page_size / block_size);
   page->is_zero = page->is_zero_init;

   // initialize an initial free list
   mpc_page_extend_free(heap, page, tld);
}


/* -----------------------------------------------------------
  Find pages with free blocks
-------------------------------------------------------------*/

// Find a page with free blocks of `page->block_size`.
static mpc_page_t* mpc_page_queue_find_free_ex(mpc_heap_t* heap, mpc_page_queue_t* pq, bool first_try)
{
   // search through the pages in "next fit" order
   size_t count = 0;
   mpc_page_t* page = pq->first;
   while (page != NULL)
   {
      mpc_page_t* next = page->next; // remember next
      count++;

      // 0. collect freed blocks by us and other threads
      _mpc_page_free_collect(page, false);

      // 1. if the page contains free blocks, we are done
      if (mpc_page_immediate_available(page)) {
         break;  // pick this one
      }

      // 2. Try to extend
      if (page->capacity < page->reserved) {
         mpc_page_extend_free(heap, page, heap->tld);
         break;
      }

      // 3. If the page is completely full, move it to the `mpc_pages_full`
      // queue so we don't visit long-lived pages too often.
      mpc_page_to_full(page, pq);

      page = next;
   } // for each page

   if (page == NULL) {
      _mpc_heap_collect_retired(heap, false); // perhaps make a page available
      page = mpc_page_fresh(heap, pq);
      if (page == NULL && first_try) {
         // out-of-memory _or_ an abandoned page with free blocks was reclaimed, try once again
         page = mpc_page_queue_find_free_ex(heap, pq, false);
      }
   }
   else {
      page->retire_expire = 0;
   }
   return page;
}



// Find a page with free blocks of `size`.
static inline mpc_page_t* mpc_find_free_page(mpc_heap_t* heap, size_t size) {
   mpc_page_queue_t* pq = mpc_page_queue(heap, size);
   mpc_page_t* page = pq->first;
   if (page != NULL) {
      _mpc_page_free_collect(page, false);

      if (mpc_page_immediate_available(page)) {
         page->retire_expire = 0;
         return page; // fast path
      }
   }
   return mpc_page_queue_find_free_ex(heap, pq, true);
}


/* -----------------------------------------------------------
  Users can register a deferred free function called
  when the `free` list is empty. Since the `local_free`
  is separate this is deterministically called after
  a certain number of allocations.
----------------------------------------------------------- */

static mpc_deferred_free_fun* volatile deferred_free = NULL;
static _Atomic(void*)deferred_arg; // = NULL

void _mpc_deferred_free(mpc_heap_t* heap, bool force) {
   heap->tld->heartbeat++;
   if (deferred_free != NULL && !heap->tld->recurse) {
      heap->tld->recurse = true;
      deferred_free(force, heap->tld->heartbeat, mpc_atomic_load_ptr_relaxed(void, &deferred_arg));
      heap->tld->recurse = false;
   }
}

void mpc_register_deferred_free(mpc_deferred_free_fun* fn, void* arg) mpc_attr_noexcept {
   deferred_free = fn;
   mpc_atomic_store_ptr_release(void, &deferred_arg, arg);
}


/* -----------------------------------------------------------
  General allocation
----------------------------------------------------------- */

// Allocate a page
static mpc_page_t* mpc_find_page(mpc_heap_t* heap, size_t size) mpc_attr_noexcept {
   // huge allocation?
   const size_t req_size = size;  // correct for padding_size in case of an overflow on `size`  
   if (mpc_unlikely(req_size > (MPC_LARGE_OBJ_SIZE_MAX))) {
      _mpc_error_message(EOVERFLOW, "allocation request is too large (%zu bytes)\n", req_size);
      return NULL;
   }
   else {
      // otherwise find a page with free blocks in our size segregated queues
      auto page = mpc_find_free_page(heap, size);
      page->getHeapID();
      return page;
   }
}

// Generic allocation routine if the fast path (`alloc.c:mpc_page_malloc`) does not succeed.
void* _mpc_malloc_generic(mpc_heap_t* heap, size_t size) mpc_attr_noexcept
{

   // initialize if necessary
   if (mpc_unlikely(!mpc_heap_is_initialized(heap))) {
      mpc_thread_init(); // calls `_mpc_heap_init` in turn
      heap = mpc_get_default_heap();
      if (mpc_unlikely(!mpc_heap_is_initialized(heap))) { return NULL; }
   }

   // call potential deferred free routines
   _mpc_deferred_free(heap, false);

   // free delayed frees from other threads
   _mpc_heap_delayed_free(heap);

   // find (or allocate) a page of the right size
   mpc_page_t* page = mpc_find_page(heap, size);
   if (mpc_unlikely(page == NULL)) { // first time out of memory, try to collect and retry the allocation once more
      mpc_heap_collect(heap, true /* force */);
      page = mpc_find_page(heap, size);
   }

   if (mpc_unlikely(page == NULL)) { // out of memory
      const size_t req_size = size;  // correct for padding_size in case of an overflow on `size`  
      _mpc_error_message(ENOMEM, "unable to allocate memory (%zu bytes)\n", req_size);
      return NULL;
   }


   // and try again, this time succeeding! (i.e. this should never recurse)
   return _mpc_page_malloc(heap, page, size);
}
