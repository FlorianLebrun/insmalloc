/*----------------------------------------------------------------------------
Copyright (c) 2018-2021, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "alloc.h"
#include "alloc-internal.h"
#include "alloc-atomic.h"

#include <string.h>  // memset, memcpy

#if defined(_MSC_VER) && (_MSC_VER < 1920)
#pragma warning(disable:4204)  // non-constant aggregate initializer
#endif

/* -----------------------------------------------------------
  Helpers
----------------------------------------------------------- */

// return `true` if ok, `false` to break
typedef bool (heap_page_visitor_fun)(mpc_heap_t* heap, mpc_page_queue_t* pq, mpc_page_t* page, void* arg1, void* arg2);

// Visit all pages in a heap; returns `false` if break was called.
static bool mpc_heap_visit_pages(mpc_heap_t* heap, heap_page_visitor_fun* fn, void* arg1, void* arg2)
{
   if (heap == NULL || heap->page_count == 0) return 0;

   // visit all pages
   size_t count = 0;
   for (size_t i = 0; i <= MPC_BIN_FULL; i++) {
      mpc_page_queue_t* pq = &heap->pages[i];
      mpc_page_t* page = pq->first;
      while (page != NULL) {
         mpc_page_t* next = page->next; // save next in case the page gets removed from the queue
         count++;
         if (!fn(heap, pq, page, arg1, arg2)) return false;
         page = next; // and continue
      }
   }
   return true;
}

/* -----------------------------------------------------------
  "Collect" pages by migrating `local_free` and `thread_free`
  lists and freeing empty pages. This is done when a thread
  stops (and in that case abandons pages if there are still
  blocks alive)
----------------------------------------------------------- */

typedef enum mpc_collect_e {
   MPC_NORMAL,
   MPC_FORCE,
   MPC_ABANDON
} mpc_collect_t;


static bool mpc_heap_page_collect(mpc_heap_t* heap, mpc_page_queue_t* pq, mpc_page_t* page, void* arg_collect, void* arg2) {
   mpc_collect_t collect = *((mpc_collect_t*)arg_collect);
   _mpc_page_free_collect(page, collect >= MPC_FORCE);
   if (mpc_page_all_free(page)) {
      // no more used blocks, free the page. 
      // note: this will free retired pages as well.
      _mpc_page_free(page, pq, collect >= MPC_FORCE);
   }
   else if (collect == MPC_ABANDON) {
      // still used blocks but the thread is done; abandon the page
      _mpc_page_abandon(page, pq);
   }
   return true; // don't break
}

static bool mpc_heap_page_never_delayed_free(mpc_heap_t* heap, mpc_page_queue_t* pq, mpc_page_t* page, void* arg1, void* arg2) {
   _mpc_page_use_delayed_free(page, MPC_NEVER_DELAYED_FREE, false);
   return true; // don't break
}

static void mpc_heap_collect_ex(mpc_heap_t* heap, mpc_collect_t collect)
{
   if (heap == NULL || !mpc_heap_is_initialized(heap)) return;
   _mpc_deferred_free(heap, collect >= MPC_FORCE);

   // note: never reclaim on collect but leave it to threads that need storage to reclaim 
   if (
#ifdef NDEBUG
      collect == MPC_FORCE
#else
      collect >= MPC_FORCE
#endif
      && _mpc_is_main_thread() && mpc_heap_is_backing(heap) && !heap->no_reclaim)
   {
      // the main thread is abandoned (end-of-program), try to reclaim all abandoned segments.
      // if all memory is freed by now, all segments should be freed.
      _mpc_abandoned_reclaim_all(heap);
   }

   // if abandoning, mark all pages to no longer add to delayed_free
   if (collect == MPC_ABANDON) {
      mpc_heap_visit_pages(heap, &mpc_heap_page_never_delayed_free, NULL, NULL);
   }

   // free thread delayed blocks.
   // (if abandoning, after this there are no more thread-delayed references into the pages.)
   _mpc_heap_delayed_free(heap);

   // collect retired pages
   _mpc_heap_collect_retired(heap, collect >= MPC_FORCE);

   // collect all pages owned by this thread
   mpc_heap_visit_pages(heap, &mpc_heap_page_collect, &collect, NULL);

   // collect segment caches
   if (collect >= MPC_FORCE) {
      _mpc_segment_thread_collect();
   }

   // collect regions on program-exit (or shared library unload)
   if (collect >= MPC_FORCE && _mpc_is_main_thread() && mpc_heap_is_backing(heap)) {
     // _mpc_mem_collect(&heap->tld->os);
   }
}

void _mpc_heap_collect_abandon(mpc_heap_t* heap) {
   mpc_heap_collect_ex(heap, MPC_ABANDON);
}

void mpc_heap_collect(mpc_heap_t* heap, bool force) mpc_attr_noexcept {
   mpc_heap_collect_ex(heap, (force ? MPC_FORCE : MPC_NORMAL));
}

void mpc_collect(bool force) mpc_attr_noexcept {
   mpc_heap_collect(mpc_get_default_heap(), force);
}


/* -----------------------------------------------------------
  Heap new
----------------------------------------------------------- */

mpc_heap_t* mpc_heap_get_default(void) {
   mpc_thread_init();
   return mpc_get_default_heap();
}

mpc_heap_t* mpc_heap_get_backing(void) {
   mpc_heap_t* heap = mpc_heap_get_default();
   mpc_heap_t* bheap = heap->tld->heap_backing;
   return bheap;
}

mpc_heap_t* mpc_heap_new(void) {
   mpc_heap_t* bheap = mpc_heap_get_backing();
   mpc_heap_t* heap = mpc_heap_malloc_tp(bheap, mpc_heap_t);  // todo: OS allocate in secure mode?
   if (heap == NULL) return NULL;
   _mpc_memcpy_aligned(heap, &_mpc_heap_empty, sizeof(mpc_heap_t));
   heap->tld = bheap->tld;
   heap->thread_id = _mpc_thread_id();
   heap->no_reclaim = true;  // don't reclaim abandoned pages or otherwise destroy is unsafe
   // push on the thread local heaps list
   heap->next = heap->tld->heaps;
   heap->tld->heaps = heap;
   return heap;
}

// zero out the page queues
static void mpc_heap_reset_pages(mpc_heap_t* heap) {
   // TODO: copy full empty heap instead?
   memset(&heap->pages_free_direct, 0, sizeof(heap->pages_free_direct));
#ifdef MPC_MEDIUM_DIRECT
   memset(&heap->pages_free_medium, 0, sizeof(heap->pages_free_medium));
#endif
   _mpc_memcpy_aligned(&heap->pages, &_mpc_heap_empty.pages, sizeof(heap->pages));
   heap->thread_delayed_free = NULL;
   heap->page_count = 0;
}

// called from `mpc_heap_destroy` and `mpc_heap_delete` to free the internal heap resources.
static void mpc_heap_free(mpc_heap_t* heap) {
   if (heap == NULL || !mpc_heap_is_initialized(heap)) return;
   if (mpc_heap_is_backing(heap)) return; // dont free the backing heap

   // reset default
   if (mpc_heap_is_default(heap)) {
      _mpc_heap_set_default_direct(heap->tld->heap_backing);
   }

   // remove ourselves from the thread local heaps list
   // linear search but we expect the number of heaps to be relatively small
   mpc_heap_t* prev = NULL;
   mpc_heap_t* curr = heap->tld->heaps;
   while (curr != heap && curr != NULL) {
      prev = curr;
      curr = curr->next;
   }
   if (curr == heap) {
      if (prev != NULL) { prev->next = heap->next; }
      else { heap->tld->heaps = heap->next; }
   }

   // and free the used memory
   sat_free(heap);
}


/* -----------------------------------------------------------
  Heap destroy
----------------------------------------------------------- */

static bool _mpc_heap_page_destroy(mpc_heap_t* heap, mpc_page_queue_t* pq, mpc_page_t* page, void* arg1, void* arg2) {

   // ensure no more thread_delayed_free will be added
   _mpc_page_use_delayed_free(page, MPC_NEVER_DELAYED_FREE, false);

   /// pretend it is all free now
   page->used = 0;

   // and free the page
   page->next = NULL;
   page->prev = NULL;
   _mpc_segment_page_free(page, false /* no force? */);

   return true; // keep going
}

void _mpc_heap_destroy_pages(mpc_heap_t* heap) {
   mpc_heap_visit_pages(heap, &_mpc_heap_page_destroy, NULL, NULL);
   mpc_heap_reset_pages(heap);
}

void mpc_heap_destroy(mpc_heap_t* heap) {
   if (heap == NULL || !mpc_heap_is_initialized(heap)) return;
   if (!heap->no_reclaim) {
      // don't free in case it may contain reclaimed pages
      mpc_heap_delete(heap);
   }
   else {
      // free all pages
      _mpc_heap_destroy_pages(heap);
      mpc_heap_free(heap);
   }
}



/* -----------------------------------------------------------
  Safe Heap delete
----------------------------------------------------------- */

// Tranfer the pages from one heap to the other
static void mpc_heap_absorb(mpc_heap_t* heap, mpc_heap_t* from) {
   if (from == NULL || from->page_count == 0) return;

   // reduce the size of the delayed frees
   _mpc_heap_delayed_free(from);

   // transfer all pages by appending the queues; this will set a new heap field 
   // so threads may do delayed frees in either heap for a while.
   // note: appending waits for each page to not be in the `MPC_DELAYED_FREEING` state
   // so after this only the new heap will get delayed frees
   for (size_t i = 0; i <= MPC_BIN_FULL; i++) {
      mpc_page_queue_t* pq = &heap->pages[i];
      mpc_page_queue_t* append = &from->pages[i];
      size_t pcount = _mpc_page_queue_append(heap, pq, append);
      heap->page_count += pcount;
      from->page_count -= pcount;
   }

   // and do outstanding delayed frees in the `from` heap  
   // note: be careful here as the `heap` field in all those pages no longer point to `from`,
   // turns out to be ok as `_mpc_heap_delayed_free` only visits the list and calls a 
   // the regular `_mpc_free_delayed_block` which is safe.
   _mpc_heap_delayed_free(from);
#if !defined(_MSC_VER) || (_MSC_VER > 1900) // somehow the following line gives an error in VS2015, issue #353
#endif

   // and reset the `from` heap
   mpc_heap_reset_pages(from);
}

// Safe delete a heap without freeing any still allocated blocks in that heap.
void mpc_heap_delete(mpc_heap_t* heap)
{
   if (heap == NULL || !mpc_heap_is_initialized(heap)) return;

   if (!mpc_heap_is_backing(heap)) {
      // tranfer still used pages to the backing heap
      mpc_heap_absorb(heap->tld->heap_backing, heap);
   }
   else {
      // the backing heap abandons its pages
      _mpc_heap_collect_abandon(heap);
   }
   mpc_heap_free(heap);
}

mpc_heap_t* mpc_heap_set_default(mpc_heap_t* heap) {
   if (heap == NULL || !mpc_heap_is_initialized(heap)) return NULL;
   mpc_heap_t* old = mpc_get_default_heap();
   _mpc_heap_set_default_direct(heap);
   return old;
}




/* -----------------------------------------------------------
  Analysis
----------------------------------------------------------- */

// static since it is not thread safe to access heaps from other threads.
static mpc_heap_t* mpc_heap_of_block(const void* p) {
   if (p == NULL) return NULL;
   return mpc_page_heap(_mpc_segment_page_of(p));
}

bool mpc_heap_contains_block(mpc_heap_t* heap, const void* p) {
   if (heap == NULL || !mpc_heap_is_initialized(heap)) return false;
   return (heap == mpc_heap_of_block(p));
}


static bool mpc_heap_page_check_owned(mpc_heap_t* heap, mpc_page_queue_t* pq, mpc_page_t* page, void* p, void* vfound) {
   bool* found = (bool*)vfound;
   void* start = _mpc_page_start(page, NULL);
   void* end = (uint8_t*)start + (page->capacity * page->xblock_size);
   *found = (p >= start && p < end);
   return (!*found); // continue if not found
}

bool mpc_heap_check_owned(mpc_heap_t* heap, const void* p) {
   if (heap == NULL || !mpc_heap_is_initialized(heap)) return false;
   if (((uintptr_t)p & (MPC_INTPTR_SIZE - 1)) != 0) return false;  // only aligned pointers
   bool found = false;
   mpc_heap_visit_pages(heap, &mpc_heap_page_check_owned, (void*)p, &found);
   return found;
}

bool mpc_check_owned(const void* p) {
   return mpc_heap_check_owned(mpc_get_default_heap(), p);
}

/* -----------------------------------------------------------
  Visit all heap blocks and areas
  Todo: enable visiting abandoned pages, and
        enable visiting all blocks of all heaps across threads
----------------------------------------------------------- */

// Separate struct to keep `mpc_page_t` out of the public interface
typedef struct mpc_heap_area_ex_s {
   mpc_heap_area_t area;
   mpc_page_t* page;
} mpc_heap_area_ex_t;

static bool mpc_heap_area_visit_blocks(const mpc_heap_area_ex_t* xarea, mpc_block_visit_fun* visitor, void* arg) {
   if (xarea == NULL) return true;
   const mpc_heap_area_t* area = &xarea->area;
   mpc_page_t* page = xarea->page;
   if (page == NULL) return true;

   _mpc_page_free_collect(page, true);
   if (page->used == 0) return true;

   const size_t bsize = page->xblock_size;
   size_t   psize;
   uint8_t* pstart = _mpc_page_start(page, &psize);

   if (page->capacity == 1) {
      // optimize page with one block
      return visitor(mpc_page_heap(page), area, pstart, bsize, arg);
   }

   // create a bitmap of free blocks.
#define MPC_MAX_BLOCKS   (MPC_SMALL_PAGE_SIZE / sizeof(void*))
   uintptr_t free_map[MPC_MAX_BLOCKS / sizeof(uintptr_t)];
   memset(free_map, 0, sizeof(free_map));

   size_t free_count = 0;
   for (mpc_block_t* block = page->freelist; block != NULL; block = mpc_block_next(page, block)) {
      free_count++;
      size_t offset = (uint8_t*)block - pstart;
      size_t blockidx = offset / bsize;  // Todo: avoid division?
      size_t bitidx = (blockidx / sizeof(uintptr_t));
      size_t bit = blockidx - (bitidx * sizeof(uintptr_t));
      free_map[bitidx] |= ((uintptr_t)1 << bit);
   }

   // walk through all blocks skipping the free ones
   size_t used_count = 0;
   for (size_t i = 0; i < page->capacity; i++) {
      size_t bitidx = (i / sizeof(uintptr_t));
      size_t bit = i - (bitidx * sizeof(uintptr_t));
      uintptr_t m = free_map[bitidx];
      if (bit == 0 && m == UINTPTR_MAX) {
         i += (sizeof(uintptr_t) - 1); // skip a run of free blocks
      }
      else if ((m & ((uintptr_t)1 << bit)) == 0) {
         used_count++;
         uint8_t* block = pstart + (i * bsize);
         if (!visitor(mpc_page_heap(page), area, block, bsize, arg)) return false;
      }
   }
   return true;
}

typedef bool (mpc_heap_area_visit_fun)(const mpc_heap_t* heap, const mpc_heap_area_ex_t* area, void* arg);


static bool mpc_heap_visit_areas_page(mpc_heap_t* heap, mpc_page_queue_t* pq, mpc_page_t* page, void* vfun, void* arg) {
   mpc_heap_area_visit_fun* fun = (mpc_heap_area_visit_fun*)vfun;
   mpc_heap_area_ex_t xarea;
   const size_t bsize = page->xblock_size;
   xarea.page = page;
   xarea.area.reserved = page->reserved * bsize;
   xarea.area.committed = page->capacity * bsize;
   xarea.area.blocks = _mpc_page_start(page, NULL);
   xarea.area.used = page->used;
   xarea.area.block_size = bsize;
   return fun(heap, &xarea, arg);
}

// Visit all heap pages as areas
static bool mpc_heap_visit_areas(const mpc_heap_t* heap, mpc_heap_area_visit_fun* visitor, void* arg) {
   if (visitor == NULL) return false;
   return mpc_heap_visit_pages((mpc_heap_t*)heap, &mpc_heap_visit_areas_page, (void*)(visitor), arg); // note: function pointer to void* :-{
}

// Just to pass arguments
typedef struct mpc_visit_blocks_args_s {
   bool  visit_blocks;
   mpc_block_visit_fun* visitor;
   void* arg;
} mpc_visit_blocks_args_t;

static bool mpc_heap_area_visitor(const mpc_heap_t* heap, const mpc_heap_area_ex_t* xarea, void* arg) {
   mpc_visit_blocks_args_t* args = (mpc_visit_blocks_args_t*)arg;
   if (!args->visitor(heap, &xarea->area, NULL, xarea->area.block_size, args->arg)) return false;
   if (args->visit_blocks) {
      return mpc_heap_area_visit_blocks(xarea, args->visitor, args->arg);
   }
   else {
      return true;
   }
}

// Visit all blocks in a heap
bool mpc_heap_visit_blocks(const mpc_heap_t* heap, bool visit_blocks, mpc_block_visit_fun* visitor, void* arg) {
   mpc_visit_blocks_args_t args = { visit_blocks, visitor, arg };
   return mpc_heap_visit_areas(heap, &mpc_heap_area_visitor, &args);
}
