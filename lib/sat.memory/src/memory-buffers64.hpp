#pragma once

namespace sat {
   class PooledBuffers64Controller : public MemorySegmentController {
   public:
      typedef union tBuffer64 {
         tBuffer64* next;
         char bytes[64];
      } *tpBuffer64;
      static_assert(sizeof(tBuffer64) == 64, "Bad sat::tBuffer64 size");

      std::atomic<tpBuffer64> Cursor;
      tpBuffer64 Levels[16];
      tpBuffer64 Limit;
      SpinLock Lock;

      void initialize();

      void* allocBufferSpanL2(uint32_t level);
      void freeBufferSpanL2(void* ptr, uint32_t level);

      virtual const char* getName() override;
   };
}
