#include <sat/memory.hpp>
#include <sat/spinlock.hpp>
#include "./memory-buffers64.hpp"
#include "../../common/bitwise.h"
#include <exception>

using namespace sat;

void PooledBuffers64Controller::initialize() {

   // Allocate buffer 64
   uintptr_t buffer64_size = 1024 * 4;
   uintptr_t buffer64_index = MemoryTableController::self.allocSegmentSpan(buffer64_size);
   if (!buffer64_index) throw std::exception("map on reserved segment has failed");
   this->Cursor = tpBuffer64(buffer64_index << sat::memory::cSegmentSizeL2);
   this->Limit = tpBuffer64((buffer64_index + buffer64_size) << sat::memory::cSegmentSizeL2);
   memset(this->Levels, 0, sizeof(this->Levels));

   // Mark buffer 64 pool
   for (uintptr_t i = 0; i < buffer64_size; i++) {
      MemoryTableController::table[buffer64_index + i] = this;
   }
}

void* PooledBuffers64Controller::allocBufferSpanL2(uint32_t level) {
   tpBuffer64 ptr;
   if (this->Levels[level]) {
      SpinLockHolder guard(this->Lock);
      ptr = this->Levels[level];
      if (ptr) {
         this->Levels[level] = ptr->next;
         return ptr;
      }
   }
   ptr = this->Cursor.fetch_add(uint64_t(1) << level);
   _ASSERT(ptr < this->Limit);
   return ptr;
}

void PooledBuffers64Controller::freeBufferSpanL2(void* ptr, uint32_t level) {
   SpinLockHolder guard(this->Lock);
   tpBuffer64(ptr)->next = this->Levels[level];
   this->Levels[level] = tpBuffer64(ptr);
}


const char* PooledBuffers64Controller::getName() {
   return "SYSTEM-OBJECT";
}
