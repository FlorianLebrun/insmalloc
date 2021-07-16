/*----------------------------------------------------------------------------
Copyright (c) 2018-2020, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

/* -----------------------------------------------------------
  Definition of page queues for each block size
----------------------------------------------------------- */

#ifndef MPC_IN_PAGE_C
#error "this file should be included from 'page.c'"
#endif

/* -----------------------------------------------------------
  Minimal alignment in machine words (i.e. `sizeof(void*)`)
----------------------------------------------------------- */

#if (MPC_MAX_ALIGN_SIZE > 4*MPC_INTPTR_SIZE)
#error "define alignment for more than 4x word size for this platform"
#elif (MPC_MAX_ALIGN_SIZE > 2*MPC_INTPTR_SIZE)
#define MPC_ALIGN4W   // 4 machine words minimal alignment
#elif (MPC_MAX_ALIGN_SIZE > MPC_INTPTR_SIZE)
#define MPC_ALIGN2W   // 2 machine words minimal alignment
#else
// ok, default alignment is 1 word
#endif


/* -----------------------------------------------------------
  Queue query
----------------------------------------------------------- */


static inline bool mpc_page_queue_is_huge(const mpc_page_queue_t* pq) {
   return (pq->block_size == (MPC_LARGE_OBJ_SIZE_MAX + sizeof(uintptr_t)));
}

static inline bool mpc_page_queue_is_full(const mpc_page_queue_t* pq) {
   return (pq->block_size == (MPC_LARGE_OBJ_SIZE_MAX + (2 * sizeof(uintptr_t))));
}

static inline bool mpc_page_queue_is_special(const mpc_page_queue_t* pq) {
   return (pq->block_size > MPC_LARGE_OBJ_SIZE_MAX);
}

/* -----------------------------------------------------------
  Bins
----------------------------------------------------------- */

// Return the bin for a given field size.
// Returns MPC_BIN_HUGE if the size is too large.
// We use `wsize` for the size in "machine word sizes",
// i.e. byte size == `wsize*sizeof(void*)`.
extern inline uint8_t _mpc_bin(size_t size) {
   size_t wsize = _mpc_wsize_from_size(size);
   uint8_t bin;
   if (wsize <= 1) {
      bin = 1;
   }
#if defined(MPC_ALIGN4W)
   else if (wsize <= 4) {
      bin = (uint8_t)((wsize + 1) & ~1); // round to double word sizes
   }
#elif defined(MPC_ALIGN2W)
   else if (wsize <= 8) {
      bin = (uint8_t)((wsize + 1) & ~1); // round to double word sizes
   }
#else
   else if (wsize <= 8) {
      bin = (uint8_t)wsize;
   }
#endif
   else if (wsize > MPC_LARGE_OBJ_WSIZE_MAX) {
      bin = MPC_BIN_HUGE;
   }
   else {
#if defined(MPC_ALIGN4W) 
      if (wsize <= 16) { wsize = (wsize + 3) & ~3; } // round to 4x word sizes
#endif
      wsize--;
      // find the highest bit
      uint8_t b = (uint8_t)mpc_bsr(wsize);  // note: wsize != 0
      // and use the top 3 bits to determine the bin (~12.5% worst internal fragmentation).
      // - adjust with 3 because we use do not round the first 8 sizes
      //   which each get an exact bin
      bin = ((b << 2) + (uint8_t)((wsize >> (b - 2)) & 0x03)) - 3;
   }
   return bin;
}



/* -----------------------------------------------------------
  Queue of pages with free blocks
----------------------------------------------------------- */

size_t _mpc_bin_size(uint8_t bin) {
   return _mpc_heap_empty.pages[bin].block_size;
}

static mpc_page_queue_t* mpc_page_queue_of(const mpc_page_t* page) {
   uint8_t bin = (mpc_page_is_in_full(page) ? MPC_BIN_FULL : _mpc_bin(page->xblock_size));
   mpc_heap_t* heap = mpc_page_heap(page);
   mpc_page_queue_t* pq = &heap->pages[bin];
   return pq;
}

static mpc_page_queue_t* mpc_heap_page_queue_of(mpc_heap_t* heap, const mpc_page_t* page) {
   uint8_t bin = (mpc_page_is_in_full(page) ? MPC_BIN_FULL : _mpc_bin(page->xblock_size));
   mpc_page_queue_t* pq = &heap->pages[bin];
   return pq;
}

// The current small page array is for efficiency and for each
// small size (up to 256) it points directly to the page for that
// size without having to compute the bin. This means when the
// current free page queue is updated for a small bin, we need to update a
// range of entries in `_mpc_page_small_free`.
static inline void mpc_heap_queue_first_update(mpc_heap_t* heap, const mpc_page_queue_t* pq) {
   size_t size = pq->block_size;
   if (size > MPC_SMALL_SIZE_MAX) return;

   mpc_page_t* page = pq->first;
   if (pq->first == NULL) page = (mpc_page_t*)&_mpc_page_empty;

   // find index in the right direct page array
   size_t start;
   size_t idx = _mpc_wsize_from_size(size);
   mpc_page_t** pages_free = heap->pages_free_direct;

   if (pages_free[idx] == page) return;  // already set

   // find start slot
   if (idx <= 1) {
      start = 0;
   }
   else {
      // find previous size; due to minimal alignment upto 3 previous bins may need to be skipped
      uint8_t bin = _mpc_bin(size);
      const mpc_page_queue_t* prev = pq - 1;
      while (bin == _mpc_bin(prev->block_size) && prev > &heap->pages[0]) {
         prev--;
      }
      start = 1 + _mpc_wsize_from_size(prev->block_size);
      if (start > idx) start = idx;
   }

   // set size range to the right page
   for (size_t sz = start; sz <= idx; sz++) {
      pages_free[sz] = page;
   }
}

/*
static bool mpc_page_queue_is_empty(mpc_page_queue_t* queue) {
  return (queue->first == NULL);
}
*/

static void mpc_page_queue_remove(mpc_page_queue_t* queue, mpc_page_t* page) {
   mpc_heap_t* heap = mpc_page_heap(page);
   if (page->prev != NULL) page->prev->next = page->next;
   if (page->next != NULL) page->next->prev = page->prev;
   if (page == queue->last)  queue->last = page->prev;
   if (page == queue->first) {
      queue->first = page->next;
      // update first
      mpc_heap_queue_first_update(heap, queue);
   }
   heap->page_count--;
   page->next = NULL;
   page->prev = NULL;
   mpc_page_set_in_full(page, false);
}


static void mpc_page_queue_push(mpc_heap_t* heap, mpc_page_queue_t* queue, mpc_page_t* page) {

   mpc_page_set_in_full(page, mpc_page_queue_is_full(queue));
   page->next = queue->first;
   page->prev = NULL;
   if (queue->first != NULL) {
      queue->first->prev = page;
      queue->first = page;
   }
   else {
      queue->first = queue->last = page;
   }

   // update direct
   mpc_heap_queue_first_update(heap, queue);
   heap->page_count++;
}


static void mpc_page_queue_enqueue_from(mpc_page_queue_t* to, mpc_page_queue_t* from, mpc_page_t* page) {

   mpc_heap_t* heap = mpc_page_heap(page);
   if (page->prev != NULL) page->prev->next = page->next;
   if (page->next != NULL) page->next->prev = page->prev;
   if (page == from->last)  from->last = page->prev;
   if (page == from->first) {
      from->first = page->next;
      // update first
      mpc_heap_queue_first_update(heap, from);
   }

   page->prev = to->last;
   page->next = NULL;
   if (to->last != NULL) {
      to->last->next = page;
      to->last = page;
   }
   else {
      to->first = page;
      to->last = page;
      mpc_heap_queue_first_update(heap, to);
   }

   mpc_page_set_in_full(page, mpc_page_queue_is_full(to));
}

// Only called from `mpc_heap_absorb`.
size_t _mpc_page_queue_append(mpc_heap_t* heap, mpc_page_queue_t* pq, mpc_page_queue_t* append) {

   if (append->first == NULL) return 0;

   // set append pages to new heap and count
   size_t count = 0;
   for (mpc_page_t* page = append->first; page != NULL; page = page->next) {
      // inline `mpc_page_set_heap` to avoid wrong assertion during absorption;
      // in this case it is ok to be delayed freeing since both "to" and "from" heap are still alive.
      mpc_atomic_store_release(&page->xheap, (uintptr_t)heap);
      // set the flag to delayed free (not overriding NEVER_DELAYED_FREE) which has as a
      // side effect that it spins until any DELAYED_FREEING is finished. This ensures
      // that after appending only the new heap will be used for delayed free operations.
      _mpc_page_use_delayed_free(page, MPC_USE_DELAYED_FREE, false);
      count++;
   }

   if (pq->last == NULL) {
      // take over afresh
      pq->first = append->first;
      pq->last = append->last;
      mpc_heap_queue_first_update(heap, pq);
   }
   else {
      // append to end
      pq->last->next = append->first;
      append->first->prev = pq->last;
      pq->last = append->last;
   }
   return count;
}
