#pragma once
#include <sat/memory/memory.hpp>

namespace sat {

   struct SegmentsAllocator : public MemorySegmentController {
   public:

      // Infos
      virtual const char* getName() override;
      void print();

      // Memory allocation
      uintptr_t allocSegments(uintptr_t size);
      void freeSegments(uintptr_t index, uintptr_t size);
      void appendSegments(uintptr_t index, uintptr_t size);
   };

   extern sat::SegmentsAllocator segments_allocator;
}

