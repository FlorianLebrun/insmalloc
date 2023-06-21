#pragma once
#include <ins/memory/map.h>
#include <ins/memory/schemas.h>

namespace ins::mem {

   struct ObjectAlivenessFlags {
      std::atomic_uint64_t flags;
   };

   struct ObjectAlivenessItem {
      uint16_t arenaID;
      std::atomic<uint32_t> next;
      std::atomic_uint64_t uncheckeds;
   };

   struct ObjectAnalysisSession {
      uint32_t* arenaIndexesMap = 0;

      ObjectAlivenessFlags* regionAlivenessMap = 0;
      ObjectAlivenessItem* regionItemsMap = 0;

      std::atomic<uint32_t> notifieds = 0;

      uint32_t allocated = 0;
      uint32_t length = 0;

      bool MarkAlive(uint16_t arenaID, uint32_t regionIndex, uint64_t objectBit);
      void Postpone(uint16_t arenaID, uint32_t regionIndex, uint64_t objectBit);
      void Reset();
      void RunOnce();
      static void MarkPtr(void* ptr);

      static ObjectAnalysisSession* enabled;
   };

   typedef struct IObjectReferenceTracker {
      virtual void MarkObjects(ObjectAnalysisSession& session) = 0;
   } *ObjectReferenceTracker;


   class ObjectMemoryCollection {
   protected:
      class iterator : ObjectInfos {
      protected:
         intptr_t arenaIndex = -1;
         intptr_t regionIndex = -1;
         intptr_t regionCount = 0;
         uint64_t regionObjects = 0;
         bool completed = false;
      public:
         iterator() : ObjectInfos(address_t()) { this->move(); }
         ~iterator() { }
         void operator ++ () { this->move(); }
         void complete() { this->completed = true; }
         bool operator != (iterator* x) { return !this->completed; }
         ObjectInfos& operator * () { return *this; }
         void move();
      };
   public:
      iterator begin() { return iterator(); }
      iterator* end() { return 0; }
   };
}
