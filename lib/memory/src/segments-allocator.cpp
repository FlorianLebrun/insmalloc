#include "./segments-allocator.hpp"
#include "./win32/system.h"
#include <sat/threads/spinlock.hpp>
#include <algorithm>
#include "../../common/bitwise.h"

using namespace sat;

static uintptr_t unsafe_allocSegments(uintptr_t size);
static uintptr_t unsafe_analyzeNotReservedSpan(uintptr_t index, uintptr_t length);

// ********************************************
//    Free spans management
// ********************************************

struct FreeSpan : MemorySegmentController {
   FreeSpan* next = 0;
   FreeSpan** pprevNext = 0;

   uint32_t index;
   uint32_t size;

   FreeSpan()
      : index(0), size(0) {
   }
   virtual const char* getName() {
      return "FREE";
   }
   virtual bool isFree() override {
      return true;
   }
   virtual bool isNotReserved() {
      return false;
   }
   FreeSpan* pull() {
      if ((*this->pprevNext) = this->next) {
         this->next->pprevNext = this->pprevNext;
      }
#if _DEBUG
      this->pprevNext = 0;
      this->next = 0;
#endif
      return this;
   }
};

struct UnusedSpan : FreeSpan {
   virtual const char* getName() {
      return "UNUSED";
   }
   virtual bool isFree() override {
      return false;
   }
   virtual bool isNotReserved() override {
      return true;
   }
};

static_assert(sizeof(FreeSpan) == 32, "Bad size");

struct FreeSpanList {
   FreeSpan* first = 0;
   void push(FreeSpan* span) {
      if (span->next = this->first) {
         this->first->pprevNext = &span->next;
      }
      span->pprevNext = &this->first;
      this->first = span;
   }
   FreeSpan* pull(size_t size, bool useBestFit) {
      FreeSpan* bestFit = 0;
      for (auto x = this->first; x; x = x->next) {
         if (x->size == size) {
            return x->pull();
         }
         else if (x->size > size) {
            if (useBestFit) {
               if (bestFit ? x->size < bestFit->size : true) bestFit = x;
            }
            else return x->pull();
         }
      }
      if (!bestFit) return 0;
      else return bestFit->pull();
   }
};

struct FreeSpanCollection {

   FreeSpanList sizings[64] = { 0 };
   uint64_t presences = 0;
   uint8_t shift = 0;

   FreeSpan* acquire(size_t size) {
      auto fittedSizeIdx = size >> this->shift;
      auto sizingMask = uint64_t(-1) << fittedSizeIdx;
      while (auto sizingBitmap = this->presences & sizingMask) {
         auto sizeIdx = lsb_64(sizingBitmap);
         auto& sizing = this->sizings[sizeIdx];
         if (auto span = sizing.pull(size, sizeIdx == fittedSizeIdx)) {
            if (!sizing.first) this->presences &= ~(uint64_t(1) << sizeIdx);
            return span;
         }
         else if (!sizing.first) { // clean invalid presence bit
            this->presences &= ~(uint64_t(1) << sizeIdx);
         }
      }
      return 0;
   }
   void release(FreeSpan* span) {
      auto sizeIdx = span->size >> this->shift;
      this->sizings[sizeIdx].push(span);
      this->presences |= uint64_t(1) << sizeIdx;
   }
};

struct FreeSpanAllocator {

   static const auto cObjectSize = sizeof(FreeSpan);
   static const int cMaxObjectPerSegment = (sat::memory::cSegmentSize / cObjectSize) - 1;

   typedef union ObjectRaw {
      ObjectRaw* next;
      char data[sizeof(FreeSpan)];
   } *tpObjectRaw;

   typedef union ObjectSlab {
      struct {
         ObjectSlab* next;
         int32_t used;
      };
      ObjectRaw entries[1];
   } *tpObjectSlab;

   FreeSpan _init_reserve[3];
   tpObjectRaw reserve = 0;
   tpObjectSlab slabs = 0;
   tpObjectSlab current = 0;

   int used_nodes_count = 0;
   int reserved_nodes_count = 0;

   FreeSpanAllocator() {
      this->reserve = 0;
      this->free(&_init_reserve[0]);
      this->free(&_init_reserve[1]);
      this->free(&_init_reserve[2]);
   }

   template <class T = FreeSpan>
   T* New() {
      _ASSERT(sizeof(T) <= cObjectSize);
      return new(this->alloc()) T();
   }

   template <class T = FreeSpan>
   void Delete(T* obj) {
      _ASSERT(sizeof(T) <= cObjectSize);
      this->free(obj);
   }

private:
   tpObjectRaw alloc() {
      this->used_nodes_count++;
      if (this->reserve) {
         auto obj = this->reserve;
         this->reserve = obj->next;
         return tpObjectRaw(obj);
      }
      else {
         if (!this->current) {
            auto index = ::unsafe_allocSegments(1);
            sat::memory::commitMemory(index, 1);
            this->current = tpObjectSlab(index << sat::memory::cSegmentSizeL2);
            this->current->used = 0;
            this->current->next = this->slabs;
            this->slabs = this->current;
            sat::memory::table[index] = &::segments_allocator;
         }
         auto obj = tpObjectRaw(&this->current[++this->current->used]);
         if (this->current->used >= cMaxObjectPerSegment) {
            this->current = 0;
         }
         this->reserved_nodes_count++;
         return obj;
      }
   }
   void free(void* obj) {
      this->used_nodes_count--;
      tpObjectRaw(obj)->next = this->reserve;
      this->reserve = tpObjectRaw(obj);
   }
};

struct FreeSpanRegistry {

   FreeSpanCollection collections[8];
   FreeSpanList largeSpansCollection;
   FreeSpanAllocator allocator;

   FreeSpanRegistry() {
      for (int i = 0; i < 8; i++) {
         this->collections[i].shift = i * 6;
      }
   }
   FreeSpan* acquire(intptr_t size) {
      _ASSERT(size > 0);
      auto collectionIdx = msb_64(size) / 6;
      if (collectionIdx < 8) {
         for (int i = collectionIdx; i < 8; i++) {
            auto span = this->collections[i].acquire(size);
            if (span) return span;
         }
      }
      return this->largeSpansCollection.pull(size, true);
   }
   void release(FreeSpan* span) {
      _ASSERT(span->size > 0);
      auto collectionIdx = msb_64(span->size) / 6;
      if (collectionIdx < 8) {
         this->collections[collectionIdx].release(span);
      }
      else {
         return this->largeSpansCollection.push(span);
      }
   }
};

// ********************************************
//    Global vars
// ********************************************

//--- Segment allocator handle
sat::SegmentsAllocator sat::segments_allocator;

//--- Segments registry
static SpinLock freelist_lock;
static FreeSpanRegistry freespans;
static uintptr_t allocated_segments = 0;


const char* SegmentsAllocator::getName() {
   return "SAT-SEGMENTS-ALLOCATOR";
}

void SegmentsAllocator::print() {
   printf("> used nodes = %d / %d\n", freespans.allocator.used_nodes_count, freespans.allocator.reserved_nodes_count);
}

static void handleOutOfMemory() {
   printf("----- out of memory -----\n");
   //this->freespans.display();
   sat::memory::table.print();
   throw std::exception("out of memory");
}

uintptr_t SegmentsAllocator::allocSegments(uintptr_t size) {
   SpinLockHolder holder(freelist_lock);
   return ::unsafe_allocSegments(size);
}

static uintptr_t unsafe_allocSegments(uintptr_t size) {
   _ASSERT(::freelist_lock.isHeld());

   //printf("\n------------------------------\nRESERVING %d..", size);memorySystem().printSegments();
   for (;;) {

      // Get a free span from memory tree
      auto span = ::freespans.acquire(size);
      if (!span) {
         ::handleOutOfMemory();
         return 0;
      }
      _ASSERT(span->size >= size);
      uintptr_t spanIndex = span->index;

      // Reserved the span
      if (span->isNotReserved()) {

         // Reserve span
         if (!SystemMemory::ReserveMemory(spanIndex << sat::memory::cSegmentSizeL2, size << sat::memory::cSegmentSizeL2)) {

            // Reanalyze span
            uintptr_t freeSize = ::unsafe_analyzeNotReservedSpan(spanIndex, span->size);

            // Trace span analysis
            // if (freeSize == spanSize) printf("span at %d is unreservable\n", uint32_t(spanIndex));
            // else printf("%d unreservable segments\n", uint32_t(spanSize - freeSize));
            continue;
         }

         // Split the unused part
         if (span->size -= size) {
            span->index += size;
            ::freespans.release(span);
         }
      }
      else {

         // Split the unused part
         if (span->size -= size) {
            span->index += size;
            ::freespans.release(span);
         }
         else {
            ::freespans.allocator.Delete(span);
         }
      }

      ::allocated_segments += size;
      return spanIndex;
   }
   return 0;
}

void SegmentsAllocator::freeSegments(uintptr_t index, uintptr_t size) {
   SpinLockHolder holder(::freelist_lock);
   FreeSpan* span = 0;

   allocated_segments -= size;
   
   // Check for previous coalescing
   auto prevEntry = sat::memory::table[index - 1];
   if (prevEntry->isFree()) {
      auto prevSpan = (FreeSpan*)prevEntry;
      prevSpan->pull();
      prevSpan->size += size;
      span = prevSpan;
   }

   // Check for next coalescing
   auto nextEntry = sat::memory::table[index + size];
   if (nextEntry->isFree()) {
      auto nextSpan = (FreeSpan*)nextEntry;
      nextSpan->pull();
      if (span) {
         span->size += nextSpan->size;
         for (size_t i = 0; i < nextSpan->size; i++) {
            sat::memory::table[nextSpan->index + i] = span;
         }
         ::freespans.allocator.Delete(nextSpan);
      }
      else {
         nextSpan->index -= size;
         nextSpan->size += size;
         span = nextSpan;
      }
   }

   // Tag the span inner as join segment
   if (!span) {
      span = ::freespans.allocator.New();
      span->index = index;
      span->size = size;
   }
   for (size_t i = 0; i < size; i++) {
      sat::memory::table[index + i] = span;
   }

   // Register when not coalesced
   ::freespans.release(span);
}

void SegmentsAllocator::appendSegments(uintptr_t index, uintptr_t size) {
   if (size) {
      SpinLockHolder holder(::freelist_lock);
      auto span = ::freespans.allocator.New<UnusedSpan>();
      span->index = index;
      span->size = size;
      for (uintptr_t i = 0; i < size; i++) {
         sat::memory::table[index + i] = span;
      }
      ::freespans.release(span);
   }
}

static uintptr_t unsafe_analyzeNotReservedSpan(uintptr_t index, uintptr_t length) { // return true when span has been split
   uintptr_t limit = index + length;
   uintptr_t freelength = 0;

   // Mark the forbidden segment
   uintptr_t cursor = index << sat::memory::cSegmentSizeL2;
   for (;;) {
      SystemMemory::tZoneState zone = SystemMemory::GetMemoryZoneState(cursor);
      cursor = zone.address + zone.size;

      // Compute zone range
      uintptr_t zoneStart = zone.address >> sat::memory::cSegmentSizeL2;
      uintptr_t zoneEnd = (zone.address + zone.size - 1) >> sat::memory::cSegmentSizeL2;
      if (zoneStart < index) zoneStart = index;

      // When is at end of range
      if (zoneEnd >= limit || zone.state == SystemMemory::OUT_OF_MEMORY) {
         uintptr_t freesize;
         if (zone.state != SystemMemory::FREE) freesize = zoneStart - index;
         else freesize = limit - index;
         if (freesize) {
            _ASSERT(freesize > 0 && index >= 32);
            auto span = ::freespans.allocator.New<UnusedSpan>();
            span->index = index;
            span->size = freesize;
            freelength += freesize;
            ::freespans.release(span);
         }
         return freelength;
      }

      // When is already used segment
      if (zone.state != SystemMemory::FREE) {

         // Mark the sat has forbidden
         for (uintptr_t i = zoneStart; i <= zoneEnd; i++) {
            sat::memory::table[i] = &ForbiddenSegmentController::self;
         }

         // Save the last freespan
         if (uintptr_t freesize = zoneStart - index) {
            _ASSERT(freesize > 0 && index >= 32);
            auto span = ::freespans.allocator.New<UnusedSpan>();
            span->index = index;
            span->size = freesize;
            freelength += freesize;
            ::freespans.release(span);
         }

         index = zoneEnd + 1;
      }
   }
}
