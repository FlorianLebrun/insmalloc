#pragma once
#include "./heaps/heaps.h"

namespace sat {

   extern bool enableObjectTracing;
   extern bool enableObjectStackTracing;
   extern bool enableObjectTimeTracing;

   extern SpinLock heaps_lock;
   extern GlobalHeap* heaps_list;
   extern GlobalHeap* heaps_table[256];

   void initializeHeaps();
   sat::GlobalHeap* createHeapCompact(const char* name);
}
