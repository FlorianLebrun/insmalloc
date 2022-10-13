#pragma once
#include <ins/memory/objects.h>
#include <ins/memory/objects-slabbed.h>

namespace ins {

   typedef struct sLargeObjectRegion : sObjectRegion {
      sObjectHeader header;
      sLargeObjectRegion(const tObjectLayoutInfos& infos);
      size_t GetSize() override;
      void DisplayToConsole() override;
   } *LargeObjectRegion;

   struct LargeObjectBucket {

   };

   /**********************************************************************
   *
   *   Large Object Class Heap
   *
   ***********************************************************************/
   struct LargeObjectHeap {
      std::mutex lock;
      LargeObjectBucket shared_objects_cache;
      uint8_t layout;

      void Initiate(uint8_t layout);
      void Clean();
   };

   /**********************************************************************
   *
   *   Large Object Class Context
   *
   ***********************************************************************/
   struct LargeObjectContext {
      LargeObjectHeap* heap = 0;
      LargeObjectBucket objects_cache;
      uint8_t layout;

      void Initiate(LargeObjectHeap* heap);
      void Clean();
      void CheckValidity();

      ObjectHeader AllocatePrivateObject(size_t size);
      ObjectHeader AllocateSharedObject(size_t size);
      void FreeObject(LargeObjectRegion region);
   };

   /**********************************************************************
   *
   *   Uncached Large Object Heap + Context
   *
   ***********************************************************************/
   struct UncachedLargeObjectProvider : public IObjectRegionOwner {
      void NotifyAvailableRegion(sObjectRegion* region) override final;
   };
   struct UncachedLargeObjectHeap : public UncachedLargeObjectProvider {
      uint8_t layout;

      void Initiate(uint8_t layout);
      LargeObjectRegion AllocateObjectRegion(size_t size);
      ObjectHeader AllocateObject(size_t size);
   };
   struct UncachedLargeObjectContext : public UncachedLargeObjectProvider {
      UncachedLargeObjectHeap* heap = 0;
      uint8_t layout;

      void Initiate(UncachedLargeObjectHeap* heap);

      ObjectHeader AllocatePrivateObject(size_t size);
      ObjectHeader AllocateSharedObject(size_t size);
   };

}
