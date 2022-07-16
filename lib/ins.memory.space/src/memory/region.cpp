#include <ins/memory/space.h>
#include <ins/memory/context.h>
#include "../os/memory.h"

using namespace ins;

uintptr_t MemoryUnit::address() {
   return (uintptr_t(this->regionID) << cRegionSizeL2) + (uintptr_t(this->position) << cUnitSizeL2);
}

void MemoryUnit::commitMemorySpan() {
   for (int i = 0; i <= this->width; i++) {
      this[i].commitMemory();
   }
}

void MemoryUnit::commitMemory() {
   if (OSMemory::CommitMemory(this->address(), cUnitSize)) {
      this->commited = -1;
#if _DEBUG
      memset((void*)this->address(), 0, cUnitSize);
#endif
   }
   else {
      throw "System Memory Error";
   }
}

bool MemoryUnit::reserveMemory() {
   if (OSMemory::ReserveMemory(this->address(), cUnitSize)) {
      this->isReserved = 1;
      return true;
   }
   else {
      _ASSERT(!this->isUsed);
      this->isUsed = 1;
      return false;
   }
}

void MemoryUnit::decommitMemory() {
   if (this->commited) {
      if (OSMemory::DecommitMemory(this->address(), cUnitSize)) {
         this->commited = 0;
      }
      else {
         throw "System Memory Error";
      }
   }
}

void MemoryUnit::releaseMemory() {
   if (this->isReserved) {
      if (OSMemory::ReleaseMemory(this->address(), cUnitSize)) {
         this->isReserved = 0;
         this->commited = 0;
      }
      else {
         throw "System Memory Error";
      }
   }
}

MemoryRegion32::MemoryRegion32(uint8_t regionID, bool restrictSmallPtrZone) {
   this->regionID = regionID;
   this->address = uintptr_t(regionID) << cRegionSizeL2;
   this->units_table[0].isFrontier = 1;
   this->units_table[1023].isFrontier = 1;
   for (int i = 0; i < 1024; i++) {
      auto& unit = this->units_table[i];
      unit.regionID = this->regionID;;
      unit.position = i;
   }
   if (restrictSmallPtrZone) {
      auto& restricted = this->units_table[0];
      restricted.isUsed = 1;
      restricted.isForbidden = 1;
      restricted.setWidth(0);

      auto& usable = this->units_table[1];
      usable.setWidth(1022);
      this->analyzeUnitFreeSpan(1);
   }
   else {
      auto& usable = this->units_table[0];
      usable.setWidth(1023);
      this->analyzeUnitFreeSpan(0);
   }
}

MemoryRegion32::~MemoryRegion32() {
   for (int i = 0; i < 1024; i++) {
      this->units_table[i].releaseMemory();
   }
}

void MemoryRegion32::check() {
   for (size_t i = 0; i < 1024; i++) {
      auto unit = &this->units_table[i];
      if (unit->width < 0) throw;
      if ((i + unit->width) >= 1024) throw;
      if (unit[unit->width].width != -unit->width) throw;
      for (int i = 1; i < unit->width - 1; i++) {
         if (unit[i].width != 0) throw;
      }
      i += unit->width;
   }
}

void MemoryRegion32::print() {
   for (size_t i = 0; i < 1024; i++) {
      auto& unit = this->units_table[i];
      auto status = unit.isUsed ? (unit.isReserved ? "used" : "forbidden") : (unit.isReserved ? "reserved" : "free");
      printf("%d-%zd\t[%zd]\t%s", this->regionID, i, size_t(1) + unit.width, status);
      if (unit.isFragmented) printf(" / fragment: x%d %s", unit.width + 1, unit.fragments_bitmap.str().c_str());
      if (unit.width < 0) printf("(invalid width)");
      else i += unit.width;
      printf("\n");
   }
}

MemoryUnit* MemoryRegion32::acquireUnitSpan(size_t length) {
   size_t width = length - 1;
retry:

   // Search best fit span
   int bestIndex = -1;
   size_t bestWidth = 1024;
   for (int i = 0; i < 1024; i++) {
      auto& unit = this->units_table[i];
      if (!unit.isUsed && unit.width >= width) {
         if (unit.width < bestWidth) {
            bestIndex = i;
            bestWidth = unit.width;
         }
      }
      i += unit.width;
   }
   if (bestIndex < 0) {
      return 0;
   }

   // Prepare unit span memory
   auto unit = &this->units_table[bestIndex];
   if (!unit->isReserved) {
      if (!this->analyzeUnitFreeSpan(bestIndex)) {
         printf("retry: changed free span\n");
         goto retry;
      }
      for (size_t i = 0; i < length; i++) {
         if (!unit[i].reserveMemory()) {
            printf("retry: changed free span\n");
            unit[0].setWidth(i - 1);
            unit[i].setWidth(bestWidth - i);
            goto retry;
         }
      }
   }

   // Fit & update units table
   if (bestWidth > width) {
      unit[width + 1].setWidth(bestWidth - width - 1);
      unit[0].setWidth(width);
   }

   // Mark unit span as used and commit
   for (size_t i = 0; i < length; i++) {
      _ASSERT(unit[i].isReserved);
      unit[i].isUsed = 1;
   }
   unit[0].setWidth(width);

   return unit;
}

bool MemoryRegion32::analyzeUnitFreeSpan(size_t index) {
   auto unit = &this->units_table[index];
   size_t length = size_t(1) + unit->width;
   bool isFullFree = true;

   // Check is in valid state
#if _DEBUG
   for (uint32_t i = 0; i <= unit->width; i++) {
      _ASSERT(!unit[i].isUsed);
      _ASSERT(!unit[i].isReserved);
   }
#endif

   // Analyze OS reservation in the freespan
   uintptr_t startAddress = unit->address();
   uintptr_t endAddress = startAddress + (length << cUnitSizeL2);
   OSMemory::EnumerateMemoryZone(startAddress, endAddress,
      [this, unit, startAddress, &isFullFree](OSMemory::tZoneState& zone) {
         if (zone.state != OSMemory::FREE) {

            // Zone range index
            uintptr_t zoneStartIndex = 0;
            uintptr_t zoneEndIndex = (zone.address + zone.size - startAddress - 1) >> cUnitSizeL2;
            if (zone.address > startAddress) zoneStartIndex = (zone.address - startAddress) >> cUnitSizeL2;
            if (zoneEndIndex > unit->width) zoneEndIndex = unit->width;

            // Tag units on zone range
            for (auto i = zoneStartIndex; i <= zoneEndIndex; i++) {
               unit[i].isUsed = 1;
               isFullFree = false;
            }
            //printf("[%.8lX] %lu Kb\n", zone.address, zone.size / 1024);
         }
      }
   );

   // Split freespan when not fully free
   if (!isFullFree) {
      uint32_t start = 0;
      while (start < length) {
         uint32_t pos = start + 1;
         auto isUsed = unit[start].isUsed;
         while (unit[pos].isUsed == isUsed && pos < length) pos++;
         unit[start].setWidth(pos - start - 1);
         start = pos;
      }
   }

   return isFullFree;
}

static MemoryUnit* getUnitLeft(MemoryUnit* unit) {
   if (!unit->isFrontier) {
      auto prev = &unit[-1];
      return &prev[prev->width];
   }
   return 0;
}

static MemoryUnit* getUnitRight(MemoryUnit* unit) {
   auto end = &unit[unit->width];
   if (!end->isFrontier) {
      return &end[1];
   }
   return 0;
}

static void coalesceUnusedsUnits(MemoryUnit* unit) {
   auto isReserved = unit->isReserved;

   // Left coalesce
   if (auto left = getUnitLeft(unit)) {
      if (!left->isUsed && left->isReserved == isReserved) {
         auto width = unit->width + left->width + 1;
         left[left->width].width = 0;
         unit->width = 0;
         left->setWidth(width);
         unit = left;
      }
   }

   // Right coalesce
   if (auto right = getUnitRight(unit)) {
      if (!right->isUsed && right->isReserved == isReserved) {
         auto width = unit->width + right->width + 1;
         unit[unit->width].width = 0;
         right->width = 0;
         unit->setWidth(width);
      }
   }
}

void MemoryRegion32::releaseUnitSpan(MemoryUnit* unit, size_t length) {
   _ASSERT(unit->regionID == this->regionID);
   _ASSERT(length > 0 && length < 1024);
   size_t width = length - 1;
   if (unit->isUsed && unit->width == width) {
      unit->isFragmented = 0;
      unit->is_misclassed = 0;
      unit->fragments_bitmap.clean();

      // Clean memory
      for (size_t i = 0; i <= width; i++) {
         _ASSERT(unit[i].isUsed && unit[i].isReserved);
         unit[i].isUsed = 0;
         unit[i].decommitMemory();
      }

      // Coalesce unused span
      coalesceUnusedsUnits(unit);
   }
   else {
      throw "invalid release request";
   }
}

address_t getPageIndex(MemoryUnit* unit, size_t position, size_t multiplier) {
   address_t address = unit->address();
   address.pageID += position * multiplier;
   return address;
}

address_t MemoryPageSpanManifold::acquirePageSpan(MemorySpace* space, size_t lengthL2) {
   static const intptr_t factor = 2;
   static const intptr_t cache_scavenge_unseen_threshold[cBinPerManifold] = {
      64 * factor, 32 * factor, 16 * factor, 8 * factor,
      4 * factor, 2 * factor, 1 * factor,
   };
   bool need_scavenge = false;
retry:

   // Search in available list
   for (intptr_t sizeID = lengthL2; sizeID < cBinPerManifold; sizeID++) {
      auto& bin = this->bins[sizeID];
      if (bin.unseenUnitCount > cache_scavenge_unseen_threshold[sizeID]) {
         need_scavenge = 1;
      }
      while (auto unit = bin.units) {

         // Check if well classified
         if (unit->is_misclassed) {
            unit->is_misclassed = 0;
            if (unit->fragments_spare_metric != sizeID) {
               auto& xbin = this->bins[unit->fragments_spare_metric];
               bin.units = unit->linknext;
               unit->linknext = xbin.units;
               xbin.units = unit;
               xbin.unseenUnitCount--;
               _ASSERT(xbin.unseenUnitCount >= 0);
            }
            else {
               bin.unseenUnitCount--;
               _ASSERT(bin.unseenUnitCount >= 0);
            }
         }

         // Try to acquire a span
         intptr_t position = unit->fragments_bitmap.acquire<AcquireMode::BestFit>(lengthL2);
         if (position >= 0) {
            return getPageIndex(unit, position, this->packing);
         }

         // Return when successfully acquired a span
         bin.units = unit->linknext;
         if (unit->fragments_bitmap.isFull())unit->fragments_spare_metric = -1;
         else unit->fragments_spare_metric--;

         auto& xbin = this->bins[unit->fragments_spare_metric];
         unit->linknext = xbin.units;
         xbin.units = unit;
      }
   }

   // Check if cache scavenging can help
   need_scavenge |= (this->bins[cBinEmptySizeId].unseenUnitCount > 0);
   if (need_scavenge) {
      this->scavengeCaches(space);
      goto retry;
   }

   // Create a new unit when nothing available
   MemoryUnit* unit = space->tryAcquireUnitSpan(this->packing);
   if (!unit) {
      space->print();
      throw;
   }
   _ASSERT(unit->width == this->packing - 1);
   _ASSERT(unit->isFragmented == 0);
   _ASSERT(unit->fragments_bitmap.isEmpty());
   _ASSERT(unit->linknext == 0);
   unit->fragments_spare_metric = 5;
   unit->isFragmented = 1;
   unit->commitMemorySpan();

   // Acquire a span
   intptr_t position = unit->fragments_bitmap.acquire<AcquireMode::FirstFit>(lengthL2);
   _ASSERT(position >= 0);

   // Register unit
   unit->linknext = this->bins[unit->fragments_spare_metric].units;
   this->bins[unit->fragments_spare_metric].units = unit;

   return getPageIndex(unit, position, this->packing);
}

void MemoryPageSpanManifold::scavengeCaches(MemorySpace* space) {
   int checked_units = 0;
   int disposed_units = 0;
   for (intptr_t i = cBinEmptySizeId; i >= -1; i--) {
      auto& cbin = this->bins[i];
      cbin.unseenUnitCount = 0;

      MemoryUnit** pPrevLink = &cbin.units;
      while (MemoryUnit* unit = *pPrevLink) {
         checked_units++;
         unit->is_misclassed = 0;
         auto new_spare_metric = unit->fragments_bitmap.getSpareSizeL2();
         if (new_spare_metric != i) {
            *pPrevLink = unit->linknext;
            unit->fragments_spare_metric = new_spare_metric;
            unit->linknext = this->bins[new_spare_metric].units;
            this->bins[new_spare_metric].units = unit;
         }
         else {
            pPrevLink = &unit->linknext;
         }
      }
   }
#if _DEBUG
   this->garbage_count++;
   if (this->garbage_count % 128 == 0) {
      printf("--------- garbage %d ---------\n", this->garbage_count);
      printf("> checked units: %d\n", checked_units);
      printf("> disposed units: %d\n", disposed_units);
   }
#endif
}

void MemoryPageSpanManifold::releasePageSpan(MemorySpace* space, address_t address, size_t lengthL2) {
   auto region = space->regions[address.unit.regionID];
   auto unit = &region->units_table[address.unit.unitID];
   unit->fragments_bitmap.release(address.unit.pageID, lengthL2);
   auto new_spare_metric = unit->fragments_bitmap.getSpareSizeL2();
   if (unit->fragments_spare_metric != new_spare_metric) {
      if (unit->is_misclassed) {
         this->bins[new_spare_metric].unseenUnitCount++;
         this->bins[unit->fragments_spare_metric].unseenUnitCount--;
         _ASSERT(this->bins[unit->fragments_spare_metric].unseenUnitCount >= 0);
      }
      else {
         unit->is_misclassed = 1;
         this->bins[new_spare_metric].unseenUnitCount++;
      }
      unit->fragments_spare_metric = new_spare_metric;
   }
}
