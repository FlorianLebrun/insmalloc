#include "./controller.h"
#include "./types/Types_database.hpp"
#include <algorithm>

using namespace sat;

sat::ControllerImpl g_SAT;

namespace ZonedBuddyAllocator {
   uintptr_t traversePageObjects(uintptr_t index, bool& visitMore, sat::IObjectVisitor* visitor);
}

namespace sat {
   extern sat::GlobalHeap* createHeapCompact(const char* name);
}

void sat::ControllerImpl::traverseObjects(sat::IObjectVisitor* visitor, uintptr_t start_address) {
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

bool sat::ControllerImpl::checkObjectsOverflow() {
   struct Visitor : sat::IObjectVisitor {
      int objectsCount;
      int invalidsCount;
      Visitor() {
         this->objectsCount = 0;
         this->invalidsCount = 0;
      }
      virtual bool visit(sat::tpObjectInfos obj) override {
         if (void* overflowPtr = obj->detectOverflow()) {
            this->invalidsCount++;
         }
         this->objectsCount++;
         return true;
      }
   };
   Visitor visitor;
   this->traverseObjects(&visitor, 0);
   return true;
}

Heap* sat::ControllerImpl::getHeap(int id) {
   if (id >= 0 && id < 256) return this->heaps_table[id];
   return 0;
}

Heap* sat::ControllerImpl::createHeap(tHeapType type, const char* name) {
   g_SAT.heaps_lock.lock();

   // Get heap id
   int heapId = -1;
   for (int i = 0; i < 256; i++) {
      if (!g_SAT.heaps_table[i]) {
         heapId = i;
         break;
      }
   }

   // Create heap
   GlobalHeap* heap = 0;
   if (heapId >= 0) {
      switch (type) {
      case tHeapType::COMPACT_HEAP_TYPE:
         heap = sat::createHeapCompact(name);
         break;
      }
      heap->heapID = heapId;
      g_SAT.heaps_table[heapId] = heap;
   }

   g_SAT.heaps_lock.unlock();
   return heap;
}

void sat::ControllerImpl::initialize() {
   // Init analysis features
   this->enableObjectTracing = false;
   this->enableObjectStackTracing = false;
   this->enableObjectTimeTracing = false;

   // Init heap table
   this->heaps_list = 0;
   memset(this->heaps_table, sizeof(this->heaps_table), 0);

   // Create types database
   sat::TypesDataBase_init();

   // Create heap 0
   g_SAT.createHeap(sat::tHeapType::COMPACT_HEAP_TYPE, "main");
}
