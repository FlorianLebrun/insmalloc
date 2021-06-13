#pragma once
#include <stdint.h>
#include <sat/memory/objects_infos.hpp>

namespace sat {
   namespace memory {

      const uintptr_t cSegmentSizeL2 = 16;
      const uintptr_t cSegmentSize = 1 << cSegmentSizeL2;
      const uintptr_t cSegmentOffsetMask = (1 << cSegmentSizeL2) - 1;
      const uintptr_t cSegmentPtrMask = ~cSegmentOffsetMask;
      const uintptr_t cSegmentMinIndex = 32;
      const uintptr_t cSegmentMinAddress = cSegmentMinIndex << cSegmentSizeL2;

      const uintptr_t cMemorySAT_MaxSpanClass = 128;

   }

   // Memory segment controller: define how handle a memory segment
   class SAT_API MemorySegmentController {
   public:
      virtual int free(uintptr_t index, uintptr_t ptr);

      // Descriptors
      virtual const char* getName() = 0;
      virtual int getHeapID() { return -1; }

      // Analysis
      virtual bool getAddressInfos(uintptr_t index, uintptr_t ptr, sat::tpObjectInfos infos) { return false; }
      virtual int traverseObjects(uintptr_t index, IObjectVisitor* visitor) { return 1; }
   };

   // Memory table entry: define the structure of segment in the table
   typedef MemorySegmentController* MemoryTableEntry;

   // Memory table controller: define the entry point for memory management
   class SAT_API MemoryTableController : public MemorySegmentController {
   public:

      static MemoryTableController self;
      static MemoryTableEntry* table;

      uint8_t bytesPerAddress; // Bytes per address (give the sizeof intptr_t)
      uint8_t bytesPerSegmentL2; // Bytes per segment (in log2)
      uintptr_t length; // Length of table
      uintptr_t limit; // Limit of table length


      virtual const char* getName() override;

      void initialize();

      // Segment Table allocation
      uintptr_t allocSegmentSpan(uintptr_t size);
      void freeSegmentSpan(uintptr_t index, uintptr_t size);
      uintptr_t reserveMemory(uintptr_t size, uintptr_t alignL2 = 1);
      void commitMemory(uintptr_t index, uintptr_t size);
      void decommitMemory(uintptr_t index, uintptr_t size);

      // Accessor
      template <typename T = MemoryTableEntry>
      static T* get(uintptr_t index) { return (T*)table[index]; }
      template <typename T = MemoryTableEntry>
      static T* set(uintptr_t index, T* entry) { return (T*)(table[index] = entry); }

      // Analysis
      void traverseObjects(sat::IObjectVisitor* visitor, uintptr_t target_address = 0);
      bool checkObjectsOverflow();

      // Helpers
      void printSegments();
   };

   class SAT_API FreeSegmentController : public MemorySegmentController {
   public:
      static FreeSegmentController self;
      virtual const char* getName() override;
   };

   class SAT_API ReservedSegmentController : public FreeSegmentController {
   public:
      static ReservedSegmentController self;
      virtual const char* getName() override;
   };

   class SAT_API ForbiddenSegmentController : public MemorySegmentController {
   public:
      static ForbiddenSegmentController self;
      virtual const char* getName() override;
   };

}
