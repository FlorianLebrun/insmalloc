#include "./segments-allocator.hpp"
#include "./memory-buffers64.hpp"
#include "./win32/system.h"
#include "../../common/alignment.h"
#include "../../common/bitwise.h"
#include <sat/memory/system-object.hpp>
#include <algorithm>
#include <string>

using namespace sat;

MemoryTableEntry* MemoryTableController::table = 0;
MemoryTableController MemoryTableController::self;

static SegmentsAllocator segments_allocator;
static PooledBuffers64Controller system_allocator;

const char* MemoryTableController::getName() {
   return "SAT";
}

void MemoryTableController::initialize() {
   if (this->table) {
      printf("sat allocator is initalized more than once.");
      return;
   }

   // Init os features
   InitSystemFeatures();

   // Compute memory limit
   uintptr_t memoryLimit = SystemMemory::GetMemorySize();
   if (sizeof(uintptr_t) > 4) {
      const uintptr_t Mb = uintptr_t(1024 * 1024);
      memoryLimit = std::min<uintptr_t>(uintptr_t(10000) * Mb, memoryLimit);
   }

   // Check memory page size
   if (sat::memory::cSegmentSize < SystemMemory::GetPageSize()) {
      throw std::exception("sat allocator cannot work with the current system memory page size");
   }

   // Initialize the table sizeings
   uintptr_t size = alignX<intptr_t>(sizeof(MemoryTableEntry) * (memoryLimit >> sat::memory::cSegmentSizeL2), sat::memory::cSegmentSize);
   this->limit = size / sizeof(MemoryTableEntry);
   this->length = this->limit;
   this->bytesPerSegmentL2 = sat::memory::cSegmentSizeL2;
   this->bytesPerAddress = sizeof(uintptr_t);

   // Create the table
   this->table = (MemoryTableEntry*)SystemMemory::AllocBuffer(sat::memory::cSegmentMinAddress, memoryLimit, size, sat::memory::cSegmentSize);
   memset(this->table, 0xff, size);

   // Mark the Forbidden zone
   for (uintptr_t i = 0; i < sat::memory::cSegmentMinIndex; i++) {
      this->table[i] = &ForbiddenSegmentController::self;
   }

   // Mark the sat with itself
   uintptr_t SATIndex = uintptr_t(this->table) >> sat::memory::cSegmentSizeL2;
   uintptr_t SATSize = size >> sat::memory::cSegmentSizeL2;
   for (uintptr_t i = 0; i < SATSize; i++) {
      this->table[SATIndex + i] = &MemoryTableController::self;
   }

   // Mark free pages
   segments_allocator.appendSegments(sat::memory::cSegmentMinIndex, SATIndex - sat::memory::cSegmentMinIndex);
   segments_allocator.appendSegments(SATIndex + SATSize, this->limit - SATIndex - SATSize);

   // Allocate buffer 64
   system_allocator.initialize();
}

uintptr_t MemoryTableController::allocSegmentSpan(uintptr_t size) {
   uintptr_t index = segments_allocator.allocSegments(size, 1);
   this->commitMemory(index, size);
   return index;
}

void MemoryTableController::freeSegmentSpan(uintptr_t index, uintptr_t size) {
   _ASSERT(size > 0);
   this->decommitMemory(index, size);
   segments_allocator.freeSegments(index, size);
}

void MemoryTableController::commitMemory(uintptr_t index, uintptr_t size) {
   uintptr_t ptr = index << sat::memory::cSegmentSizeL2;

   // Commit the segment memory
   if (!SystemMemory::CommitMemory(ptr, size << sat::memory::cSegmentSizeL2)) {
      this->printSegments();
      throw std::exception("commit on reserved segment has failed");
   }

}

void MemoryTableController::decommitMemory(uintptr_t index, uintptr_t size) {
   uintptr_t ptr = index << sat::memory::cSegmentSizeL2;

   // Commit the segment memory
   if (!SystemMemory::DecommitMemory(ptr, size << sat::memory::cSegmentSizeL2)) {
      throw std::exception("decommit on reserved segment has failed");
   }
}

uintptr_t MemoryTableController::reserveMemory(uintptr_t size, uintptr_t alignL2) {
   return segments_allocator.allocSegments(size, alignL2);
}

sat::memory::sizeID_t sat::memory::getSystemSizeID(size_t size) {
   return sizeID_t::with(msb_32(uint32_t(size)) + 1);
}

void* sat::memory::allocSystemBuffer(sizeID_t sizeID) {
   return system_allocator.allocBufferSpanL2(sizeID);
}

void sat::memory::freeSystemBuffer(void* ptr, sizeID_t sizeID) {
   return system_allocator.freeBufferSpanL2(ptr, sizeID);

}

static uintptr_t SATEntryToString(uintptr_t index) {
   using namespace sat::memory;
   auto& entry = MemoryTableController::table[index];
   uintptr_t len;
   for (len = 0; (index + len) < MemoryTableController::self.length && MemoryTableController::table[index + len] == entry; len++);

   std::string name = entry->getName();
   printf("\n[ %.5d to %.5d ] %s", uint32_t(index), uint32_t(index + len - 1), name.c_str());

   auto heapID = entry->getHeapID();
   if (heapID >= 0) {
      struct Visitor : sat::IObjectVisitor {
         int objectsCount = 0;
         virtual bool visit(sat::tpObjectInfos obj) override {
            this->objectsCount++;
            return true;
         }
      } visitor;
      entry->traverseObjects(index, &visitor);
      printf(" / heap=%d, objects=%d", heapID, visitor.objectsCount);
   }
   return len;
}

void MemoryTableController::printSegments() {
   uintptr_t i = 0;
   std::string name;
   printf("\nSegment Allocation Table:");
   while (i < MemoryTableController::self.length) {
      i += SATEntryToString(i);
   }
   printf("\n");
}

void MemoryTableController::traverseObjects(sat::IObjectVisitor* visitor, uintptr_t start_address) {
   int i = start_address >> sat::memory::cSegmentSizeL2;
   bool visitMore = true;
   while (i < MemoryTableController::self.length && visitMore) {
      auto controller = MemoryTableController::table[i];
      if (controller->getHeapID() >= 0) {
         i += controller->traverseObjects(i, visitor);
      }
      else i++;
   }
}

bool MemoryTableController::checkObjectsOverflow() {
   struct Visitor : sat::IObjectVisitor {
      size_t objectsCount = 0;
      size_t invalidsCount = 0;
      virtual bool visit(sat::tpObjectInfos obj) override {
         if (void* overflowPtr = obj->detectOverflow()) {
            this->invalidsCount++;
         }
         this->objectsCount++;
         return true;
      }
   } visitor;
   this->traverseObjects(&visitor, 0);
   return true;
}

int MemorySegmentController::free(uintptr_t index, uintptr_t ptr) {
   printf("Cannot free ptr in '%s'\n", this->getName());
   return 0;
}

FreeSegmentController FreeSegmentController::self;

const char* FreeSegmentController::getName() {
   return "FREE";
}

ReservedSegmentController ReservedSegmentController::self;

const char* ReservedSegmentController::getName() {
   return "FREE-RESERVED";
}

ForbiddenSegmentController ForbiddenSegmentController::self;

const char* ForbiddenSegmentController::getName() {
   return "FORBIDDEN";
}

