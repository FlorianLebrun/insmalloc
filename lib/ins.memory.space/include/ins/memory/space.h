#pragma once
#include <mutex>
#include <stdint.h>
#include <ins/binary/bitmap64.h>
#include <ins/memory/descriptors.h>
#include <ins/memory/configuration.h>
#include <ins/memory/region.h>
#include <ins/macros.h>

namespace ins {

   struct ElementClassStats {
      sizeid_t element_size = 0;
      size_t element_used_count = 0;
      size_t element_cached_count = 0;
      size_t slab_empty_count = 0;
      size_t slab_full_count = 0;
      size_t slab_count = 0;
   };

   struct MemoryStats {
      size_t used_count = 0;
      size_t cached_count = 0;
      size_t used_size = 0;
      size_t cached_size = 0;
   };

   struct MemorySpace {

      struct SlabBin {
         tpSlabBatchDescriptor batches = 0;
         sizeid_t element_size;
         std::mutex lock;
         tpSlabDescriptor pop(MemorySpace* space, uint8_t context_id);
         void scavenge(MemorySpace* space);
         void getStats(ElementClassStats& stats);
      };

      MemoryRegion32* regions[cRegionPerSpace] = { 0 }; // 1Tb = 256 regions of 32bits address space (4Gb)
      MemoryPageSpanManifold pageSpans_manifolds[cPackingCount];
      SlabBin slabs_bins[cSlabBinCount];

      MemoryContext* contexts = 0;
      std::mutex contexts_lock;

      MemorySpace();
      ~MemorySpace();

      // Region management
      MemoryRegion32* createRegion(size_t regionID);

      // Unit aligned allocation
      MemoryUnit* acquireUnitSpan(size_t length);
      MemoryUnit* tryAcquireUnitSpan(size_t length);

      // Page aligned allocation
      address_t acquirePageSpan(size_t length);
      address_t acquirePageSpan(size_t packing, size_t shift);
      void releasePageSpan(address_t base, size_t length);

      // Context
      void registerContext(MemoryContext* context);
      void unregisterContext(MemoryContext* context);

      // Garbaging
      void scavengeCaches();

      // Helpers
      void getStats();
      void print();
      void check();
   };
   
   void printAllBlocks();
}
