#include "./stack-stamp.h"

namespace sat {

   sat::StackStampDatabase* getStackStampDatabase() {
      static sat::StackStampDatabase* stackStampDatabase = nullptr;
      static sat::SpinLock lock;
      if (!stackStampDatabase) {
         sat::SpinLockHolder guard(lock);
         if (!stackStampDatabase) {
            stackStampDatabase = sat::StackStampDatabase::create();
         }
      }
      return stackStampDatabase;
   }

   StackStampDatabase* StackStampDatabase::create() {
      uintptr_t index = sat::memory::allocSegmentSpan(1);
      auto object = (StackStampDatabase*)(index << memory::cSegmentSizeL2);
      object->StackStampDatabase::StackStampDatabase(uintptr_t(&object[1]));
      return object;
   }

   StackStampDatabase::StackStampDatabase(uintptr_t firstSegmentBuffer)
      : stacktree(this, firstSegmentBuffer)
   {
   }

   const char* StackStampDatabase::getName() {
      return "STACKSTAMP_DATABASE";
   }

   void StackStampDatabase::traverseStack(uint64_t stackstamp, sat::IStackVisitor* visitor) {
      typedef StackNode<tData>* Node;
      for (auto node = Node(stackstamp); node; node = node->parent) {
         visitor->visit(node->marker);
      }
   }
}
