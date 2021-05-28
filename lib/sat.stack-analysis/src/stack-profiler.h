#pragma once
#include <sat/system-object.hpp>
#include "./scheduler.h"
#include "./stack-stamp.h"
#include "./thread-stack-tracker.h"

namespace sat {

   struct StackProfiling : public SystemObject<IStackProfiling>::Derived<StackProfiling>, public MemoryTableController {
      StackTree<tData> stacktree;
      virtual const char* getName() override;
      virtual IStackProfiling::Node getRoot() override;
      virtual void print() override;
      static StackProfiling* create();
   private:
      StackProfiling();
   };

   struct ThreadStackProfiler : public SystemObject<IStackProfiler>::Derived<ThreadStackProfiler>, public sat::TickWorker {

      Thread* target;
      ThreadStackTracker tracker;
      StackProfiling* profiling;
      uint64_t lastSampleTime;

      ThreadStackProfiler(Thread* thread);
      virtual ~ThreadStackProfiler() override;

      virtual void execute() override;

      virtual IStackProfiling* flushProfiling() override;
      virtual void startProfiling() override;
      virtual void stopProfiling() override;
   };
}
