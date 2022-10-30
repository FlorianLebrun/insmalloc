#pragma once
#include <ins/memory/objects-base.h>

namespace ins {

   struct ObjectCentralContext;
   struct ObjectLocalContext;
   struct MemoryContext;
   struct MemoryCentralContext;

   union ObjectAllocOptions {
      struct {
         uint8_t enableTimeStamp : 1;
         uint8_t enableStackStamp : 1;
         uint8_t enableSecurityPadding;
      };
      struct {
         uint8_t enableAnalytics : 2;
      };
      uint16_t enableds = 0;
   };

   struct ObjectRegionCache {
      // TODO
   };

   /**********************************************************************
   *
   *   ObjectCentralContext
   *   (multithreaded memory context/heap of an object class)
   *
   ***********************************************************************/

   struct ObjectCentralContext {

      struct CentralObjects {
         std::mutex lock;

         // Used regions list
         ObjectRegionList usables; // Region with available objects
         ObjectRegionList disposables; // Region with no used objects

         // Notified region for checking
         ObjectRegionNotifieds notifieds;
      };

      bool managed = false;
      std::uint64_t objects_notifieds_warnings;
      CentralObjects objects[ins::cst::ObjectLayoutCount];
      ObjectRegionCache regions[ins::cst::ObjectRegionTemplateCount];

      void Initiate(bool managed);
      void CheckValidity();
      void Clean();

      void PushDisposableRegion(ObjectRegion region);
      void PushUsableRegion(ObjectRegion region);

      bool ScavengeNotifiedRegions(uint8_t layoutID);
      void ReceiveDisposables(uint8_t layoutID, ObjectRegionList& disposables);
   };

   /**********************************************************************
   *
   *   ObjectLocalContext
   *   (monothreaded memory hap of an object class)
   *
   ***********************************************************************/

   struct ObjectLocalContext {

      struct LocalObjects {
         ObjectRegion active_region = 0; // Current used region
         ObjectRegionList usables; // Usable regions for next active
         ObjectRegionNotifieds notifieds; // Notified region for checking
         void RemoveActiveRegion();
      };

      bool managed = false;
      MemoryContext* context = 0;
      ObjectCentralContext* heap = 0;

      LocalObjects privateds[ins::cst::ObjectLayoutCount];
      LocalObjects shareds[ins::cst::ObjectLayoutCount];
      ObjectRegionList disposables[ins::cst::ObjectLayoutCount]; // Region with no used objects

      ObjectRegionCache regions[ins::cst::ObjectRegionTemplateCount];

      void Initiate(MemoryContext* context, ObjectCentralContext* central);
      void Clean();

      ObjectHeader AllocatePrivatedObject(size_t size);
      ObjectHeader AllocateSharedObject(size_t size);
      ObjectHeader AllocateLargeObject(size_t size, bool privated);
      ObjectHeader AllocateInstrumentedObject(size_t size, bool privated, ObjectAllocOptions options);

      void PushDisposableRegion(uint8_t layoutID, ObjectRegion region);
      void PushUsableRegion(ObjectRegion region);

   protected:
      ObjectHeader AcquirePrivatedObject(uint8_t layoutID);
      ObjectHeader AcquireSharedObject(uint8_t layoutID);

      bool PullActivePrivatedRegion(uint8_t layoutID);
      bool PullActiveSharedRegion(uint8_t layoutID);

      bool ScavengeNotifiedRegions(uint8_t layoutID);
      uint32_t ScavengeNotifiedRegions(ObjectRegion region);
   };
}
