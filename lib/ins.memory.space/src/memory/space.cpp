#include <ins/memory/space.h>
#include <ins/memory/context.h>
#include "../os/memory.h"

using namespace ins;

MemorySpace::MemorySpace() {
   for (int packing = 1; packing <= 7; packing += 2) {
      this->pageSpans_manifolds[packing >> 1].packing = packing;
   }

   for (int i = 0; i < cSlabBinCount; i++) {
      SlabClass* cls = cSlabBinTable[i];
      this->slabs_bins[i].element_size = cls->getSlabSize();
   }

   this->createRegion(1);
}

MemorySpace::~MemorySpace() {
   for (size_t i = 0; i < 256; i++) {
      if (auto region = this->regions[i]) {
         region->MemoryRegion32::~MemoryRegion32();
         OSMemory::ReleaseMemory(uintptr_t(region), cPageSize);
         this->regions[i] = 0;
      }
   }
}

MemoryRegion32* MemorySpace::createRegion(size_t regionID) {
   _ASSERT(!this->regions[regionID]);
   auto base = regionID << cRegionSizeL2;
   auto region = (MemoryRegion32*)OSMemory::AllocBuffer(base, base + cRegionSize, cUnitSize, cUnitSize);
   _ASSERT(region);
   OSMemory::CommitMemory(uintptr_t(region), cPageSize);
   region->MemoryRegion32::MemoryRegion32(regionID);
   return this->regions[regionID] = region;
}

MemoryUnit* MemorySpace::tryAcquireUnitSpan(size_t length) {
   for (size_t i = 0; i < 256; i++) {
      if (auto region = this->regions[i]) {
         auto result = region->acquireUnitSpan(length);
         if (result) return result;
      }
   }
   return 0;
}

MemoryUnit* MemorySpace::acquireUnitSpan(size_t length) {
   MemoryUnit* unit = this->tryAcquireUnitSpan(length);
   if (!unit) {
      this->scavengeCaches();
      unit = this->tryAcquireUnitSpan(length);
      if (!unit) {
         this->print();
         printf(" /!\\ Missing free unit span\n");
      }
   }
   return unit;
}

address_t MemorySpace::acquirePageSpan(size_t packing, size_t shift) {
   if (shift < cPagePerUnitL2) {
      MemoryPageSpanManifold& manifold = this->pageSpans_manifolds[packing >> 1];
      return manifold.acquirePageSpan(this, shift);
   }
   else {
      auto unit = this->acquireUnitSpan(packing << (shift - cPagePerUnitL2));
      return unit->address();
   }
}

address_t MemorySpace::acquirePageSpan(size_t length) {
   const uintptr_t cMaxSpanSizeForFragmentedUnit = 7 << cPagePerUnitL2;

   if (length <= cMaxSpanSizeForFragmentedUnit) {
      size_target_t target(length);
      MemoryPageSpanManifold& manifold = this->pageSpans_manifolds[target.packing >> 1];
      return manifold.acquirePageSpan(this, target.shift);
   }
   else {
      auto unit = this->acquireUnitSpan(alignX(length, cPagePerUnit) >> cPagePerUnitL2);
      return unit->address();
   }
}

void MemorySpace::releasePageSpan(address_t base, size_t length) {
   _ASSERT(length > 0);
   auto region = this->regions[base.unit.regionID];
   auto unit = &region->units_table[base.unit.unitID];
   if (unit->isFragmented) {
      size_target_t target(length);
      this->pageSpans_manifolds[target.packing].releasePageSpan(this, base, target.shift);
   }
   else {
      region->releaseUnitSpan(unit, base.unit.pageID);
   }
}

void MemorySpace::registerContext(MemoryContext* context) {
   if (context->space != 0 || context->id != 0) throw;
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   if (!this->contexts || this->contexts->id > 1) {
      context->id = 1;
      context->next_context = this->contexts;
      context->space = this;
      this->contexts = context;
   }
   else {
      for (auto ctx = this->contexts; ctx; ctx = ctx->next_context) {
         if (!ctx->next_context || ctx->next_context->id != ctx->id + 1) {
            context->id = ctx->id + 1;
            context->next_context = ctx->next_context;
            context->space = this;
            ctx->next_context = context;
            break;
         }
      }
   }
}

void MemorySpace::unregisterContext(MemoryContext* context) {
   if (context->space != this || context->id == 0) throw;

   // Clean context memory cache
   context->scavenge();

   // Remove context
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   for (auto pctx = &this->contexts; *pctx; pctx = &(*pctx)->next_context) {
      if (*pctx == context) {
         *pctx = context->next_context;
         context->id = 0;
         context->space = 0;
         context->next_context = 0;
         return;
      }
   }
   throw "not found";
}

void MemorySpace::scavengeCaches() {
   for (int i = 0; i < cSlabBinCount; i++) {
      auto& bin = this->slabs_bins[i];
      bin.scavenge(this);
   }
   for (int i = 0; i < cPackingCount; i++) {
      this->pageSpans_manifolds[i].scavengeCaches(this);
   }
}

void MemorySpace::check() {
   for (size_t i = 0; i < 256; i++) {
      if (auto region = this->regions[i]) {
         region->check();
      }
   }
}

void MemorySpace::print() {
   printf("------- begin -------\n");
   for (size_t i = 0; i < 256; i++) {
      if (auto region = this->regions[i]) {
         region->print();
      }
   }
   printf("------- end -------\n");
}

void MemorySpace::getStats() {
   int slabCacheSize = 0;
   int slabEmptySpanCount = 0;
   for (int i = 0; i < cSlabBinCount; i++) {
      ElementClassStats stats;
      auto cls = cSlabBinTable[i];
      auto& bin = this->slabs_bins[i];
      bin.getStats(stats);
      auto cache_size = stats.element_cached_count * bin.element_size.size();
      printf("slab '%d': count=%zd, empty_batch=%zd/%zd\n", i, stats.element_cached_count, stats.slab_empty_count, stats.slab_count);
      slabCacheSize += cache_size;
   }
   printf("> slab cache=%d\n", slabCacheSize);
}

void MemorySpace::SlabBin::getStats(ElementClassStats& stats) {
   for (auto batch = this->batches; batch; batch = batch->next) {
      auto uses = batch->uses;
      stats.element_cached_count += Bitmap64(batch->usables ^ uses).count();
      if (uses == batch->usables) stats.slab_full_count++;
      if (uses == 0) stats.slab_empty_count++;
      stats.slab_count++;
   }
}

void MemorySpace::SlabBin::scavenge(MemorySpace* space) {
   std::lock_guard<std::mutex> guard(this->lock);
   auto pprev = &this->batches;
   while (auto batch = *pprev) {
      if (batch->uses == 0) {
         *pprev = batch->next;
         auto cls = cBlockClassTable[batch->class_id];
         printf("lost batch\n");
      }
      else {
         pprev = &(*pprev)->next;
      }
   }
}

tpSlabDescriptor MemorySpace::SlabBin::pop(MemorySpace* space, uint8_t context_id) {
   std::lock_guard<std::mutex> guard(this->lock);
   if (auto batch = this->batches) {

      // Find index and tag it
      uint64_t availables = batch->usables ^ batch->uses;
      SAT_ASSERT(availables != 0);
      size_t index = lsb_64(availables);
      batch->uses |= uint64_t(1) << index;
      size_t position = index + 1;

      // On batch is full
      if (batch->uses == batch->usables) {
         this->batches = batch->next;
      }

      // Format slab
      auto slab = tpSlabDescriptor(&batch[position]);
      slab->context_id = context_id;
      slab->class_id = 0;
      slab->block_ratio_shift = 0;
      slab->slab_position = position;
      slab->page_index = batch->page_index;
      slab->gc_marks = 0;
      slab->gc_analyzis = 0;
      slab->uses = 0;
      slab->usables = 0;
      slab->shared_freemap = 0;
      slab->next = 0;

      return slab;
   }
   return 0;
}
