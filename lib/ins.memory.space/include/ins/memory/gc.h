#pragma once
#include <mutex>
#include <vector>
#include <stdint.h>
#include <ins/memory/context.h>

#if _DEBUG
#define USE_SAT_DEBUG 1
#else
#define USE_SAT_DEBUG 0
#endif

#if USE_SAT_DEBUG
#define SAT_DEBUG(x) x
#define SAT_ASSERT(x) _ASSERT(x)
#define SAT_INLINE _ASSERT(x)
#define SAT_PROFILE __declspec(noinline)
#else
#define SAT_DEBUG(x)
#define SAT_ASSERT(x)
#define SAT_PROFILE
#endif

namespace ins {

   /* Garbage Collector
   * --------------------------------------------
   *
   * Each element managed by the garbage has two status:
   *  - Marked: element is selected to be kept alive during sweep phase
   *  - NeedAnalysis: element reference shall been analyzed, so when a reference change this flag shall be reset to 1
   *
   * When the NeedAnalysis is set from 0 to 1, the element shall be added to the context current analysis list
   *
   */

   extern bool GC_analysing;

   template <typename T>
   class ref {
      T* ptr = 0;
   public:
      T* operator = (T* ptr) {
         if (GC_analysing) this->write_barrier(ptr);
         else this->ptr = ptr;
      }
      void write_barrier(T* ptr) {
         if (this->ptr) GC->mark(this->ptr);
         if (this->ptr = ptr) GC->mark(ptr);
      }
   };

   struct GCSlabList {
      GCSlabList* next;
      tpSlabDescriptor* slabs[15];
   };

   class GarbageCollector {
   public:
      // Globally collected analysis list
      std::vector<tpSlabDescriptor> analysisList;
      MemorySpace* space;

      std::vector<void*> roots;

      GarbageCollector(MemorySpace* space) : space(space) {

      }
      void sweep_block(uintptr_t ptr, size_t size) {
         printf("garbage object: %p\n", ptr);
      }
      void sweep_slab(tpSlabDescriptor slab) {
         uint64_t iterated_bits = slab->uses ^ slab->gc_marks;
         iterated_bits |= slab->shared_freemap.exchange(0);
         _ASSERT(slab->gc_analyzis == 0);
         slab->uses ^= iterated_bits;
         slab->gc_marks = 0;
         if (iterated_bits) {
            size_t index = 0;
            auto base = uintptr_t(slab->page_index) << ins::cPageSizeL2;
            if (slab->slab_position) base += uintptr_t(slab->slab_position - 1) << (32 - slab->block_ratio_shift);
            auto cls = cBlockClassTable[slab->class_id];
            size_t size = cls->getBlockSize().size();
            while (iterated_bits) {
               auto shift = lsb_64(iterated_bits);
               index += shift;
               this->sweep_block(base + index * size, size);
               iterated_bits >>= shift + 1;
               index++;
            }
         }
      }
      void sweep() {
         for (int r = 0; r < cRegionPerSpace; r++) {
            if (auto region = space->regions[r]) {
               for (int s = 0; s < cPagePerRegion; s++) {
                  auto& entry = region->pages_table[s];
                  if (auto slab = tpSlabDescriptor(entry.reference)) {
                     this->sweep_slab(slab);
                  }
               }
            }
         }
      }
      void mark(address_t ptr) {
         BlockLocation loc(this->space, ptr);
         uint64_t bitmask = uint64_t(1) << loc.index;
         if (loc.descriptor && loc.descriptor->uses & bitmask) {
            if ((loc.descriptor->gc_marks & bitmask) == 0) {
               loc.descriptor->gc_marks |= bitmask;
               loc.descriptor->gc_analyzis |= bitmask;
               if (loc.descriptor->needAnalysis == 0) {
                  loc.descriptor->needAnalysis |= 1;
                  analysisList.push_back(loc.descriptor);
               }
            }
         }
         else {
            // Invalid ptr
         }
      }
      void scan_object(uintptr_t ptr) {

      }
      void scan_block(uintptr_t ptr, size_t size) {
         auto bytes = (const char*)ptr;
         auto end = bytes + size - sizeof(void*);
         while (bytes < end) {
            this->mark(*(uintptr_t*)bytes);
            bytes++;
         }
      }
      void analyze_slab(tpSlabDescriptor slab) {
         uint64_t iterated_bits = slab->gc_analyzis;
         slab->gc_analyzis = 0;
         if (iterated_bits) {
            size_t index = 0;
            auto base = uintptr_t(slab->page_index) << ins::cPageSizeL2;
            if (slab->slab_position) base += uintptr_t(slab->slab_position - 1) << (32 - slab->block_ratio_shift);
            auto cls = cBlockClassTable[slab->class_id];
            size_t size = cls->getBlockSize().size();
            while (iterated_bits) {
               auto shift = lsb_64(iterated_bits);
               index += shift;
               this->scan_block(base + index * size, size);
               iterated_bits >>= shift + 1;
               index++;
            }
         }
      }
      void analyze() {
         for (auto root : this->roots) {
            this->mark(root);
         }
         while (this->analysisList.size()) {
            auto slab = this->analysisList.back();
            this->analysisList.pop_back();
            slab->needAnalysis = 0;
            this->analyze_slab(slab);
         }
      }
      void scavenge() {
         printf("--- begin GC ---\n");
         this->analyze();
         this->sweep();
         printf("--- end GC -----\n");
         printf("----------------\n");
      }
   };

}
