#pragma once
#include <ins/memory/map.h>
#include <ins/memory/contexts.h>
#include <ins/memory/analysis.h>

namespace ins::mem {

   /**********************************************************************
   *
   *   Heap Controler
   *
   ***********************************************************************/

   struct StarvedConsumerToken {
      std::mutex lock;
      std::condition_variable signal;
      StarvedConsumerToken* next = 0;
      size_t expectedByteLength = 0;
      MemoryContext* context = 0;
   };

   enum class tHeapIssue {
      FreeOutOfBoundObject,
      FreeInexistingObject,
      FreeRetainedObject,
   };

   extern void InitializeHeap();

   // Memory allocation context API
   //--------------------------------------------------
   extern MemoryContext* AcquireContext(bool isShared);
   extern MemoryCentralContext& AcquireCentralContext();
   extern void DisposeContext(MemoryContext* context);

   // Memory profiling API
   //--------------------------------------------------
   extern void SetTimeStampOption(bool enabled);
   extern void SetStackStampOption(bool enabled);
   extern void SetSecurityPaddingOption(uint32_t paddingSize);

   // Maintenance API
   //--------------------------------------------------
   extern void PerformHeapCleanup();
   extern void MarkAndSweepUnusedObjects();
   extern void RescueStarvedConsumer(StarvedConsumerToken& token);
   extern void ScheduleContextRecovery(MemoryContext* context);
   extern void NotifyHeapIssue(tHeapIssue issue, address_t addr);

   extern void RegisterReferenceTracker(ObjectReferenceTracker tracker);
   extern void UnregisterReferenceTracker(ObjectReferenceTracker tracker);

   // Debug helpers API
   //--------------------------------------------------
   struct tObjectsStats {
      size_t region_count = 0;

      size_t used_bytes = 0;
      size_t notified_bytes = 0;
      size_t avaiblable_bytes = 0;
      size_t total_bytes = 0;

      size_t used_objects = 0;
      size_t notified_objects = 0;
      size_t avaiblable_objects = 0;
      size_t total_objects = 0;

      void add(ObjectRegion region);
      void add(tObjectsStats& stat);
      void print();
   };

   extern tObjectsStats GetObjectsStats();
   extern void CheckValidity();
   extern void PrintInfos();
}
