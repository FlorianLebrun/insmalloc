#pragma once
#include <ins/binary/bitwise.h>
#include <ins/memory/space.h>
#include <ins/memory/descriptors.h>

namespace ins {

   struct BlockLocation {
      tpSlabDescriptor descriptor;
      uint32_t index;

      BlockLocation(tpSlabDescriptor descriptor = 0, uint32_t index = 0)
         :descriptor(descriptor), index(index) {
      }
      BlockLocation(MemorySpace* space, address_t address) {
         this->set(space, address);
      }
      SAT_PROFILE void set(MemorySpace* space, address_t address) {
         auto region = address.regionID < cRegionPerSpace ? space->regions[address.regionID] : 0;
         if (region) {
            auto& entry = space->regions[address.regionID]->pages_table[address.pageID];
            auto& layout = ins::cPageLayouts[entry.layoutID];
            auto block_ratio = (uintptr_t(address.position) + layout.offset) * layout.scale;
            this->descriptor = &tpSlabDescriptor(entry.reference)[block_ratio >> 32];
            this->index = (block_ratio & 0xFFFFFFFF) >> this->descriptor->block_ratio_shift;
         }
         else {
            this->descriptor = 0;
            this->index = 0;
         }
      }
      operator bool() {
         return !!this->descriptor;
      }
   };

   class MemoryContext {
   public:
      struct BlockBin {
         tpSlabDescriptor slabs = 0;
         sizeid_t element_size;
         SAT_PROFILE address_t pop();
         void scavenge(MemoryContext* context);
         void getStats(ElementClassStats& stats);
      };

      uint8_t id = 0;
      MemorySpace* space = 0;
      MemoryContext* next_context = 0;
      BlockBin blocks_bins[cBlockBinCount];

      MemoryContext(MemorySpace* space);
      ~MemoryContext();
      address_t allocateBlock(size_t size);
      void disposeBlock(address_t ptr);

      // System block allocation with 64 bytes packing
      // note: length64 is a number of contigious 64 bytes chunks
      void* allocateSystemMemory(size_t length64);
      void releaseSystemMemory(void* base, size_t length64);

      void scavenge();

      void getStats();
   };

   // Controller for secondary thread which have no heavy use of this allocator
   struct SharedMemoryContext : MemoryContext {
      std::mutex lock;
      address_t allocate(size_t size) {
         std::lock_guard<std::mutex> guard(lock);
         return this->MemoryContext::allocateBlock(size);
      }
   };
}
