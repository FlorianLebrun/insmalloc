#pragma once
#include <stdint.h>
#include <sat/memory/objects_infos.hpp>

namespace sat {

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
      virtual bool isFree() { return false; }
   };

   // Memory table: define the entry point for memory management
   class SAT_API MemoryTable {
   public:
      typedef MemorySegmentController* Entry;

      Entry* entries = 0;
      uint8_t bytesPerAddress = 0; // Bytes per address (give the sizeof intptr_t)
      uint8_t bytesPerSegmentL2 = 0; // Bytes per segment (in log2)
      uintptr_t length = 0; // Length of table
      uintptr_t limit = 0; // Limit of table length

      void initialize();

      // Accessors
      template <typename T = Entry>
      T* get(uintptr_t index) { return (T*)this->entries[index]; }
      template <typename T = Entry>
      T* set(uintptr_t index, T* entry) { return (T*)(this->entries[index] = entry); }
      Entry& operator [] (uintptr_t index) { return this->entries[index]; }

      // Helpers
      void print();
   };

   // Memory table controller: define the entry point for memory management
   class SAT_API MemoryTableController : public MemorySegmentController {
   public:
      static MemoryTableController self;
      virtual const char* getName() override;
   };

   class SAT_API ForbiddenSegmentController : public MemorySegmentController {
   public:
      static ForbiddenSegmentController self;
      virtual const char* getName() override;
   };

   struct SAT_API memory {

      static const uintptr_t cSegmentSizeL2 = 16;
      static const uintptr_t cSegmentSize = 1 << cSegmentSizeL2;
      static const uintptr_t cSegmentOffsetMask = (1 << cSegmentSizeL2) - 1;
      static const uintptr_t cSegmentPtrMask = ~cSegmentOffsetMask;
      static const uintptr_t cSegmentMinIndex = 32;
      static const uintptr_t cSegmentMinAddress = cSegmentMinIndex << cSegmentSizeL2;

      static const uintptr_t cMemorySAT_MaxSpanClass = 128;

      // Segment allocation
      static uintptr_t allocSegmentSpan(uintptr_t size);
      static void freeSegmentSpan(uintptr_t index, uintptr_t size);

      static void commitMemory(uintptr_t index, uintptr_t size);
      static void decommitMemory(uintptr_t index, uintptr_t size);

      // Segment Table Accessors
      static MemoryTable table;

      // Analysis
      static void traverseObjects(sat::IObjectVisitor* visitor, uintptr_t target_address = 0);
      static bool checkObjectsOverflow();
   }; 
}
