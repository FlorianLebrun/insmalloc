/* ----------------------------------------------------------------------------
Copyright (c) 2018-2020, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "alloc.h"
#include "alloc-internal.h"
#include "alloc-atomic.h"

#include <string.h>  // memset
#include <stdio.h>

#define MPC_PAGE_HUGE_ALIGN  (256*1024)

/* -----------------------------------------------------------
 Segment size calculations
----------------------------------------------------------- */

// Raw start of the page available memory; can be used on uninitialized pages (only `segment_idx` must be set)
// The raw start is not taking aligned block allocation into consideration.
static uint8_t* mpc_segment_raw_page_start(const mpc_page_t* page, size_t* page_size) {
   size_t   psize = page->getPageSize();
   uint8_t* p = (uint8_t*)page;

   {
      // the first page starts after the segment info (and possible guard page)
      p += sizeof(mpc_page_t);
      psize -= sizeof(mpc_page_t);
   }

   if (page_size != NULL) *page_size = psize;
   return p;
}

// Start of the page available memory; can be used on uninitialized pages (only `segment_idx` must be set)
uint8_t* _mpc_segment_page_start(const mpc_page_t* page, size_t block_size, size_t* page_size, size_t* pre_size)
{
   size_t   psize;
   uint8_t* p = mpc_segment_raw_page_start(page, &psize);
   if (pre_size != NULL) *pre_size = 0;
   if (block_size > 0 && page->page_kind <= MPC_PAGE_MEDIUM) {
      // for small and medium objects, ensure the page start is aligned with the block size (PR#66 by kickunderscore)
      size_t adjust = block_size - ((uintptr_t)p % block_size);
      if (adjust < block_size) {
         p += adjust;
         psize -= adjust;
         if (pre_size != NULL) *pre_size = adjust;
      }
   }

   if (page_size != NULL) *page_size = psize;
   return p;
}

// called by threads that are terminating to free cached segments
void _mpc_segment_thread_collect() {
   // nothing
}


/* -----------------------------------------------------------
   Free
----------------------------------------------------------- */

void _mpc_segment_page_free(mpc_page_t* page, bool force)
{
   page->disposePage();
}

/* -----------------------------------------------------------
   Abandon segment/page
----------------------------------------------------------- */

void _mpc_segment_page_abandon(mpc_page_t* page) {
   throw;
}

/* -----------------------------------------------------------
  Reclaim abandoned pages
----------------------------------------------------------- */

void _mpc_abandoned_reclaim_all(mpc_heap_t* heap) {
   throw;
}


/* -----------------------------------------------------------
   large page allocation
----------------------------------------------------------- */

// free huge block from another thread
void _mpc_segment_huge_page_free(mpc_page_t* page, mpc_block_t* block) {
   throw;
}

/* -----------------------------------------------------------
   Page allocation
----------------------------------------------------------- */

struct mpc_small_page_s : mpc_page_s {
   static const auto cPageSize = MPC_SMALL_PAGE_SIZE;
   static const auto cPageSegments = cPageSize >> sat::memory::cSegmentSizeL2;

   mpc_small_page_s(mpc_heap_t* heap, size_t block_size) {
      this->page_kind = MPC_PAGE_SMALL;
      this->thread_id = heap->thread_id;
   }
   virtual const char* getName() override {
      return "MI-SMALL";
   }
   virtual size_t getPageSize() const override {
      return MPC_SMALL_PAGE_SIZE;
   }  
   virtual void disposePage() override {
      sat::memory::freeSegmentSpan(uintptr_t(this) >> sat::memory::cSegmentSizeL2, cPageSegments);
   }
};

struct mpc_medium_page_s : mpc_page_s {
   static const auto cPageSize = MPC_MEDIUM_PAGE_SIZE;
   static const auto cPageSegments = cPageSize >> sat::memory::cSegmentSizeL2;

   mpc_medium_page_s(mpc_heap_t* heap, size_t block_size) {
      this->page_kind = MPC_PAGE_MEDIUM;
      this->thread_id = heap->thread_id;
   }
   virtual const char* getName() override {
      return "MI-MEDIUM";
   };
   virtual size_t getPageSize() const override {
      return cPageSize;
   }
   virtual void disposePage() override {
      sat::memory::freeSegmentSpan(uintptr_t(this) >> sat::memory::cSegmentSizeL2, cPageSegments);
   }
};

static mpc_page_t* mpc_segment_small_page_alloc(mpc_heap_t* heap, size_t block_size, mpc_os_tld_t* os_tld) {
   auto length = MPC_SMALL_PAGE_SIZE >> sat::memory::cSegmentSizeL2;
   auto index = sat::memory::allocSegmentSpan(length);
   auto page = (mpc_small_page_s*)(index << sat::memory::cSegmentSizeL2);
   page->mpc_small_page_s::mpc_small_page_s(heap, block_size);
   for (auto i = 0; i < length; i++) sat::memory::table[i + index] = page;
   return page;
}

static mpc_page_t* mpc_segment_medium_page_alloc(mpc_heap_t* heap, size_t block_size, mpc_os_tld_t* os_tld) {
   auto length = MPC_MEDIUM_PAGE_SIZE >> sat::memory::cSegmentSizeL2;
   auto index = sat::memory::allocSegmentSpan(length);
   auto page = (mpc_medium_page_s*)(index << sat::memory::cSegmentSizeL2);
   page->mpc_medium_page_s::mpc_medium_page_s(heap, block_size);
   for (auto i = 0; i < length; i++) sat::memory::table[i + index] = page;
   return page;
}

mpc_page_t* _mpc_segment_page_alloc(mpc_heap_t* heap, size_t block_size, mpc_os_tld_t* os_tld) {
   if (block_size <= MPC_SMALL_OBJ_SIZE_MAX) {
      return mpc_segment_small_page_alloc(heap, block_size, os_tld);
   }
   else if (block_size <= MPC_MEDIUM_OBJ_SIZE_MAX) {
      return mpc_segment_medium_page_alloc(heap, block_size, os_tld);
   }
   else if (block_size <= MPC_LARGE_OBJ_SIZE_MAX) {
      throw;
   }
   return 0;
}
