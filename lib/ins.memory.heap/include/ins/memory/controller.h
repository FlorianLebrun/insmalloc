#pragma once
#include <ins/memory/regions.h>
#include <ins/memory/schemas.h>
#include <ins/memory/contexts.h>

namespace ins::mem {

   /**********************************************************************
   *
   *   Region Consumer
   *
   ***********************************************************************/

   struct StarvedConsumerToken {
      std::mutex lock;
      std::condition_variable signal;
      StarvedConsumerToken* next = 0;
      size_t expectedByteLength = 0;
      MemoryContext* context = 0;
   };

   struct MemoryController {
      std::mutex notification_lock;
      std::condition_variable notification_signal;
      std::thread worker;
      bool terminating = false;

      std::mutex contexts_lock;
      uint16_t contexts_count = 0;
      MemoryContext* contexts = 0;
      MemorySharedContext* defaultContext = 0;

      MemoryCentralContext central;

      MemoryContext* recovered_contexts = 0;
      StarvedConsumerToken* starved_consumers = 0;

      ObjectAnalysisSession cleanup;
      uint32_t cycle = 0;

      MemoryController();
      ~MemoryController();
      void Initiate();
      void CheckValidity();
      void Print();

      void RescueStarvedConsumer(StarvedConsumerToken& token);
      void ScheduleContextRecovery(MemoryContext* context);
      void NotifyWorker();

      MemorySharedContext* CreateSharedContext();

      MemoryContext* AcquireContext();
      void DisposeContext(MemoryContext* context);

      void SetTimeStampOption(bool enabled);
      void SetStackStampOption(bool enabled);
      void SetSecurityPaddingOption(uint32_t paddingSize);

      void PerformMemoryCleanup();
      void MarkAndSweepUnusedObjects();
   private:
      void MarkUsedObjects();
      void SweepUnusedObjects();
   };

   extern MemoryController Controller;
}
