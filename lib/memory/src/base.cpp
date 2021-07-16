#include "./segments-allocator.hpp"
#include "./win32/system.h"
#include "../../common/alignment.h"
#include "../../common/bitwise.h"
#include <algorithm>
#include <string>

sat::MemoryTable sat::memory::table;

void sat::MemoryTable::initialize() {
   if (this->entries) {
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
   uintptr_t size = alignX<intptr_t>(sizeof(MemoryTable::Entry) * (memoryLimit >> sat::memory::cSegmentSizeL2), sat::memory::cSegmentSize);
   this->limit = size / sizeof(MemoryTable::Entry);
   this->length = this->limit;
   this->bytesPerSegmentL2 = sat::memory::cSegmentSizeL2;
   this->bytesPerAddress = sizeof(uintptr_t);

   // Create the table
   this->entries = (MemoryTable::Entry*)SystemMemory::AllocBuffer(sat::memory::cSegmentMinAddress, memoryLimit, size, sat::memory::cSegmentSize);
   memset(this->entries, 0xff, size);

   // Mark the Forbidden zone
   for (uintptr_t i = 0; i < sat::memory::cSegmentMinIndex; i++) {
      this->entries[i] = &ForbiddenSegmentController::self;
   }

   // Mark the sat with itself
   uintptr_t SATIndex = uintptr_t(this->entries) >> sat::memory::cSegmentSizeL2;
   uintptr_t SATSize = size >> sat::memory::cSegmentSizeL2;
   for (uintptr_t i = 0; i < SATSize; i++) {
      this->entries[SATIndex + i] = &MemoryTableController::self;
   }

   // Mark free pages
   segments_allocator.appendSegments(sat::memory::cSegmentMinIndex, SATIndex - sat::memory::cSegmentMinIndex);
   segments_allocator.appendSegments(SATIndex + SATSize, this->limit - SATIndex - SATSize);
}

uintptr_t sat::memory::allocSegmentSpan(uintptr_t size) {
   uintptr_t index = segments_allocator.allocSegments(size);
   sat::memory::commitMemory(index, size);
   return index;
}

void sat::memory::freeSegmentSpan(uintptr_t index, uintptr_t size) {
   _ASSERT(size > 0);
   sat::memory::decommitMemory(index, size);
   segments_allocator.freeSegments(index, size);
}

void sat::memory::commitMemory(uintptr_t index, uintptr_t size) {
   uintptr_t ptr = index << sat::memory::cSegmentSizeL2;

   // Commit the segment memory
   if (!SystemMemory::CommitMemory(ptr, size << sat::memory::cSegmentSizeL2)) {
      sat::memory::table.print();
      throw std::exception("commit on reserved segment has failed");
   }

}

void sat::memory::decommitMemory(uintptr_t index, uintptr_t size) {
   uintptr_t ptr = index << sat::memory::cSegmentSizeL2;

   // Commit the segment memory
   if (!SystemMemory::DecommitMemory(ptr, size << sat::memory::cSegmentSizeL2)) {
      throw std::exception("decommit on reserved segment has failed");
   }
}

static uintptr_t SATEntryToString(uintptr_t index) {
   auto& entry = sat::memory::table[index];
   uintptr_t len;
   for (len = 0; (index + len) < sat::memory::table.length && sat::memory::table[index + len] == entry; len++);

   std::string name = entry->getName();
   printf("\n[0x%.6X0000] %8.0lf Kb %s", uint32_t(index), double(len * 64), name.c_str());

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

void sat::MemoryTable::print() {
   uintptr_t i = 0;
   std::string name;
   printf("\nSegment Allocation Table:");
   while (i < this->length) {
      i += SATEntryToString(i);
   }
   printf("\n");
   segments_allocator.print();
}

void sat::memory::traverseObjects(sat::IObjectVisitor* visitor, uintptr_t start_address) {
   int i = start_address >> sat::memory::cSegmentSizeL2;
   bool visitMore = true;
   while (i < sat::memory::table.length && visitMore) {
      auto controller = sat::memory::table[i];
      if (controller->getHeapID() >= 0) {
         i += controller->traverseObjects(i, visitor);
      }
      else i++;
   }
}

bool sat::memory::checkObjectsOverflow() {
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
   sat::memory::traverseObjects(&visitor, 0);
   return true;
}

int sat::MemorySegmentController::free(uintptr_t index, uintptr_t ptr) {
   printf("Cannot free ptr in '%s'\n", this->getName());
   return 0;
}

sat::MemoryTableController sat::MemoryTableController::self;

const char* sat::MemoryTableController::getName() {
   return "SAT";
}

sat::ForbiddenSegmentController sat::ForbiddenSegmentController::self;

const char* sat::ForbiddenSegmentController::getName() {
   return "FORBIDDEN";
}

