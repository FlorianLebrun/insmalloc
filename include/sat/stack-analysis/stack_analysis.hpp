#pragma once
#include <stdint.h>
#include <sat/memory/system-object.hpp>
#include <sat/threads/thread.hpp>

namespace sat {

   //-----------------------------------------------------------------------------
   // Stack Marker: define the sealed shape of a stack beacon
   //-----------------------------------------------------------------------------

   struct IStackMarker {
      virtual void getSymbol(char* buffer, int size) = 0;
   };

   struct StackMarker : IStackMarker {
      uint64_t word64[3];
      template<typename MarkerT>
      MarkerT* as() {
         _ASSERT(sizeof(typename MarkerT) <= sizeof(StackMarker));
         this->word64[0] = 0;
         this->word64[1] = 0;
         this->word64[2] = 0;
         return new(this) typename MarkerT();
      }
      intptr_t compare(StackMarker& other) {
         intptr_t c;
         if (c = other.word64[0] - this->word64[0]) return c;
         if (c = other.word64[-1] - this->word64[-1]) return c;
         if (c = other.word64[1] - this->word64[1]) return c;
         if (c = other.word64[2] - this->word64[2]) return c;
         return 0;
      }
      void copy(StackMarker& other) {
         this->word64[-1] = other.word64[-1];
         this->word64[0] = other.word64[0];
         this->word64[1] = other.word64[1];
         this->word64[2] = other.word64[2];
      }
      virtual void getSymbol(char* buffer, int size) override {
         buffer[0] = 0;
      }
   };
   static_assert(sizeof(StackMarker) == 4 * sizeof(uint64_t), "StackMarker size invalid");

   //-----------------------------------------------------------------------------
   // Stack Beacon: define a frame of a metadata stack, allocated on the call stack
   //-----------------------------------------------------------------------------

   struct SAT_API StackBeacon {
      uint64_t beaconId;
      virtual void begin() final;
      virtual void end() final;
      virtual bool isSpan() { return false; }
      virtual void createrMarker(StackMarker& buffer) = 0;
   };

   class StackBeaconHolder {
      StackBeacon& beacon;
   public:
      StackBeaconHolder(StackBeacon& beacon) :beacon(beacon) { beacon.begin(); }
      ~StackBeaconHolder() { beacon.end(); }
   };

   //-----------------------------------------------------------------------------
   // Stack profiling
   //-----------------------------------------------------------------------------

   template <class tData>
   struct StackNode {
      StackNode* next;
      StackNode* parent;
      StackNode* children;
      sat::StackMarker marker;
      tData data;
   };

   struct IStackStampBuilder {
      virtual void push(sat::StackMarker& marker) = 0;
   };

   struct IStackVisitor {
      virtual void visit(StackMarker& marker) = 0;
   };

   struct IStackProfiling : public ISystemObject {

      struct tData {
         int hits;
         tData() : hits(0) {}
      };

      typedef sat::StackNode<tData>* Node;

      virtual Node getRoot() = 0;
      virtual void print() = 0;
   };

   struct IStackProfiler : public ISystemObject {
      virtual IStackProfiling* flushProfiling() = 0;
      virtual void startProfiling() = 0;
      virtual void stopProfiling() = 0;
   };

   namespace analysis {
      SAT_API IStackProfiler* createStackProfiler(Thread* thread);
      SAT_API void traverseStack(uint64_t stackstamp, IStackVisitor* visitor);
      SAT_API uint64_t getCurrentStackStamp();
      SAT_API void printStackStamp(uint64_t stackstamp);
   }
}
