#include <ins/memory/space.h>
#include <ins/memory/context.h>
#include "./blocks.h"

#include <functional>
#include <atomic>

using namespace ins;

INS_DEBUG(static MemorySpace* g_space = 0);

MemoryContext::MemoryContext(MemorySpace* space) {
   INS_DEBUG(g_space = space);
   space->registerContext(this);
   for (int i = 0; i < cBlockBinCount; i++) {
      BlockClass* cls = cBlockBinTable[i];
      this->blocks_bins[i].element_size = cls->getBlockSize();
   }
}

MemoryContext::~MemoryContext() {
   space->unregisterContext(this);
}

void MemoryContext::BlockBin::getStats(ElementClassStats& stats) {
   for (auto slab = this->slabs; slab; slab = slab->next) {
      auto uses = slab->uses ^ slab->shared_freemap;
      stats.element_cached_count += Bitmap64(slab->usables ^ uses).count();
      if (uses == slab->usables) stats.slab_full_count++;
      if (uses == 0) stats.slab_empty_count++;
      stats.slab_count++;
   }
}

void MemoryContext::BlockBin::scavenge(MemoryContext* context) {
   auto pprev = &this->slabs;
   while (auto slab = *pprev) {
      slab->uses ^= slab->shared_freemap.exchange(0);
      if (slab->uses == 0) {
         *pprev = slab->next;
         auto cls = cBlockClassTable[slab->class_id];
         cls->getSlabClass()->release(slab, context);
      }
      else {
         pprev = &(*pprev)->next;
      }
   }
}

address_t MemoryContext::BlockBin::pop() {
   while (auto slab = this->slabs) {

      // Compute available block in slab
      uint64_t availables = slab->usables ^ slab->uses;
      if (availables == 0) {
         availables = slab->shared_freemap.exchange(0);
         if (availables == 0) {
            this->slabs = slab->next;
            slab->next = 0;
            slab->context_id = 0;
            continue;
         }
         slab->uses ^= availables;
      }

      // Find index and tag it
      INS_ASSERT(availables != 0);
      auto index = lsb_64(availables);
      slab->uses |= uint64_t(1) << index;

      // Compute block address
      uintptr_t block_index = uintptr_t(index);
      if (slab->slab_position) block_index += uintptr_t(slab->slab_position - 1) << (32 - slab->block_ratio_shift);
      uintptr_t ptr = (block_index * this->element_size.packing) << this->element_size.shift;
      ptr += uintptr_t(slab->page_index) << ins::cPageSizeL2;

      INS_DEBUG(BlockLocation loc(g_space, ptr));
      INS_ASSERT(loc.descriptor == slab);
      INS_ASSERT(loc.index == index);
      return ptr;
   }
   return nullptr;
}

void* MemoryContext::allocateSystemMemory(size_t length64) {
   return malloc(64 * length64);
}

void MemoryContext::releaseSystemMemory(void* base, size_t length64) {
   return free(base);
}

address_t MemoryContext::allocateBlock(size_t size) {
   size_target_t target(size);
   auto cls = ins::getBlockClass(target);
   INS_ASSERT(size <= cls->getSizeMax());
   //printf("allocate %d for %d (lost = %lg%%)\n", cls->getSizeMax(), size, trunc(100*double(cls->getSizeMax() - size) / double(cls->getSizeMax())));
   auto addr = cls->allocate(size, this);
   INS_DEBUG(memset(addr, 0xdd, size));
   return addr;
}

void MemoryContext::disposeBlock(address_t address) {
   if (address.regionID != 1) throw;
   ins::BlockLocation block(this->space, address);
   auto slab = block.descriptor;

   uint64_t bit = uint64_t(1) << block.index;
   INS_ASSERT(slab->uses & bit);

   if (slab->context_id == this->id) {
      slab->uses ^= bit;
      if (slab->uses == 0) {
         auto cls = ins::cBlockClassTable[slab->class_id];
         //cls->receiveEmptySlab(slab, this);
         //printf("empty slab %.8X size:%d\n", address.ptr, cls->getSizeMax());
      }
   }
   else {
      auto prev_freemap = slab->shared_freemap.fetch_or(bit);
      if (prev_freemap == 0 && slab->context_id == 0) {
         _ASSERT(slab->context_id == 0);
         auto cls = ins::cBlockClassTable[slab->class_id];
         slab->context_id = this->id;
         cls->receivePartialSlab(slab, this);
      }
      else {
         //printf("cross-free %d->%d\n", this->id, slab->context_id);
         slab->shared_freemap.fetch_xor(bit);
         //throw "todo";
      }
   }
}

void MemoryContext::scavenge() {
   for (int i = 0; i < cBlockBinCount; i++) {
      auto& bin = this->blocks_bins[i];
      bin.scavenge(this);
   }
}

void MemoryContext::getStats() {
   int blockCacheSize = 0;
   for (int i = 0; i < cBlockBinCount; i++) {
      ElementClassStats stats;
      auto cls = cBlockBinTable[i];
      auto& bin = this->blocks_bins[i];
      bin.getStats(stats);
      auto cache_size = stats.element_cached_count * bin.element_size.size();
      printf("block '%d': count=%d, empty_slab=%d/%d\n", bin.element_size.size(), stats.element_cached_count, stats.slab_empty_count, stats.slab_count);
      blockCacheSize += cache_size;
   }

   printf("> block cache=%d\n", blockCacheSize);
}