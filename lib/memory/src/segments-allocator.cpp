#include "./segments-allocator.hpp"
#include "./win32/system.h"
#include <sat/memory/system-object.hpp>
#include <algorithm>

using namespace sat::memory;

namespace sat {

   const uint64_t cKeyNotReserved = 0x80000000;
   const sizeID_t BtNodeSizeId = getSystemSizeID(64);

   uint64_t GetKeyIndex(uint64_t key) {
      return (key & 0x7fffffff);
   }
   uint64_t GetKeySize(uint64_t key) {
      return (key >> 32);
   }
   uint64_t IsKeyNotReserved(uint64_t key) {
      return (key & cKeyNotReserved);
   }
   uint64_t MakeKey(uint64_t index, uint64_t size) {
      _ASSERT(index >= 32);
      return (size << 32) | index;
   }
   uint64_t MakeKeyFromSize(uint64_t size) {
      return (size << 32);
   }
   uint64_t MakeKeyFromIndex(uint64_t index) {
      return index;
   }

   FreeSegmentsTree::FreeSegmentsTree() {
      this->reserve = 0;
      this->freeNode(&_init_reserve[0]);
      this->freeNode(&_init_reserve[1]);
      this->freeNode(&_init_reserve[2]);
   }

   FreeSegmentsTree::BtNode* FreeSegmentsTree::allocNode() {
      if (this->reserve) {
         BtNode* node = this->reserve;
         this->reserve = node->p[0];
         return node;
      }
      else {
         return (BtNode*)allocSystemBuffer(BtNodeSizeId);
      }
   }

   void FreeSegmentsTree::freeNode(BtNode* node) {
      node->p[0] = this->reserve;
      this->reserve = node;
   }

   SegmentsAllocator::SegmentsAllocator() {
      this->allocated_segments = 0;
   }

   void SegmentsAllocator::handleOutOfMemory() {
      printf("----- out of memory -----\n");
      //this->freespans.display();
      sat::MemoryTableController::self.printSegments();
      throw std::exception("out of memory");
   }

   uintptr_t SegmentsAllocator::allocSegments(uintptr_t size, uintptr_t alignL2) {
      uintptr_t spanIndex = 0, spanSize = 0;

      //printf("\n------------------------------\nRESERVING %d..", size);memorySystem().printSegments();
      this->freelist_lock.lock();
      for (;;) {

         // Get a free span from memory tree
         uint64_t spanKey = 0;
         if (!this->freespans.removeUpper(MakeKeyFromSize(size), spanKey)) {
            this->handleOutOfMemory();
            return 0;
         }

         // Reserved the span
         uintptr_t spanIndex = GetKeyIndex(spanKey);
         uintptr_t spanSize = GetKeySize(spanKey);
         _ASSERT(spanSize >= size);
         if (IsKeyNotReserved(spanKey)) {

            // Reserve span
            if (!SystemMemory::ReserveMemory(spanIndex << sat::memory::cSegmentSizeL2, size << sat::memory::cSegmentSizeL2)) {

               // Reanalyze span
               uintptr_t freeSize = this->analyzeNotReservedSpan(spanIndex, spanSize);

               // Trace span analysis
               // if (freeSize == spanSize) printf("span at %d is unreservable\n", uint32_t(spanIndex));
               // else printf("%d unreservable segments\n", uint32_t(spanSize - freeSize));
               continue;
            }

            // Split the unused part
            if (uintptr_t remainSize = spanSize - size) {
               this->freespans.insert(MakeKey(spanIndex + size, remainSize) | cKeyNotReserved);
            }
         }
         else {

            // Split the unused part
            if (uintptr_t remainSize = spanSize - size) {
               this->freespans.insert(MakeKey(spanIndex + size, remainSize));
            }
         }

         this->allocated_segments += size;
         this->freelist_lock.unlock();
         return spanIndex;
      }
      this->freelist_lock.unlock();
      return 0;
   }

   void SegmentsAllocator::freeSegments(uintptr_t index, uintptr_t size) {
      for (uintptr_t i = 0; i < size; i++) {
         MemoryTableController::table[index + i] = &ReservedSegmentController::self;
      }
      this->freelist_lock.lock();
      this->allocated_segments -= size;
      this->freespans.insert(MakeKey(index, size));
      this->freelist_lock.unlock();
   }

   void SegmentsAllocator::appendSegments(uintptr_t index, uintptr_t size) {
      for (uintptr_t i = 0; i < size; i++) {
         MemoryTableController::table[index + i] = &FreeSegmentController::self;
      }
      this->freelist_lock.lock();
      this->freespans.insert(MakeKey(index, size) | cKeyNotReserved);
      this->freelist_lock.unlock();
   }

   uintptr_t SegmentsAllocator::analyzeNotReservedSpan(uintptr_t index, uintptr_t length) { // return true when span has been split
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
               freelength += freesize;
               this->freespans.insert(MakeKey(index, freesize) | cKeyNotReserved);
            }
            return freelength;
         }

         // When is already used segment
         if (zone.state != SystemMemory::FREE) {

            // Mark the sat has forbidden
            auto table = MemoryTableController::table;
            for (uintptr_t i = zoneStart; i <= zoneEnd; i++) {
               _ASSERT(table[i] == &FreeSegmentController::self || table[i] == &ReservedSegmentController::self);
               table[i] = &ForbiddenSegmentController::self;
            }

            // Save the last freespan
            if (uintptr_t freesize = zoneStart - index) {
               _ASSERT(freesize > 0 && index >= 32);
               freelength += freesize;
               this->freespans.insert(MakeKey(index, freesize) | cKeyNotReserved);
            }

            index = zoneEnd + 1;
         }
      }
   }
}
