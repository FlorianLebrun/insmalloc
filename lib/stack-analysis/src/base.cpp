#include <sat/stack-analysis/stack_analysis.hpp>
#include <sat/heaps/allocator.hpp>
#include "./stack-stamp.h"
#include "./stack-profiler.h"
#include "./thread-stack-tracker.h"

bool has_stackframe_order(void* higher, void* lower) {
   return uintptr_t(higher) < uintptr_t(lower);
}

__forceinline sat::ThreadStackTracker* getCurrentStackTracker() {
   sat::Thread* thread = sat::Thread::current();
   auto tracker = thread->getObject<sat::ThreadStackTracker>();
   if (!tracker) {
      tracker = new sat::ThreadStackTracker(thread);
      thread->setObject<sat::ThreadStackTracker>(tracker);
   }
   return tracker;
}

void sat::StackBeacon::begin() {
   auto tracker = getCurrentStackTracker();
   if (tracker->stackBeacons >= tracker->stackBeaconsBase) {
      this->beaconId = ++tracker->stackLastId;
      (--tracker->stackBeacons)[0] = this;
   }
   else {
      printf("Error in sat stack beacon: too many beacon.\n");
   }
}

void sat::StackBeacon::end() {
   auto tracker = getCurrentStackTracker();
   while (tracker->stackBeacons[0] && has_stackframe_order(tracker->stackBeacons[0], this)) {
      tracker->stackBeacons++;
      printf("Error in sat stack beacon: a previous beacon has been not closed.\n");
   }
   if (tracker->stackBeacons[0] && tracker->stackBeacons[0] == this) {
      tracker->stackBeacons++;
   }
   else if (tracker->stackBeacons != tracker->stackBeaconsBase) {
      printf("Error in sat stack beacon: a beacon is not retrieved and cannot be properly closed.\n");
   }
}

namespace sat {
   namespace analysis {
      IStackProfiler* createStackProfiler(Thread* thread) {
         return new ThreadStackProfiler(thread);
      }
      void traverseStack(uint64_t stackstamp, IStackVisitor* visitor) {
         auto stackdb = getStackStampDatabase();
         if (stackdb) {
            stackdb->traverseStack(stackstamp, visitor);
         }
      }
      uint64_t getCurrentStackStamp() {
         auto tracker = getCurrentStackTracker();
         return tracker->getStackStamp();
      }
      void printStackStamp(uint64_t stackstamp) {
         struct Visitor : public sat::IStackVisitor {
            virtual void visit(sat::StackMarker& marker) override {
               char symbol[1024];
               marker.getSymbol(symbol, sizeof(symbol));
               printf("* %s\n", symbol);
            }
         } visitor;
         sat::getStackStampDatabase()->traverseStack(stackstamp, &visitor);
      }

   }
}
