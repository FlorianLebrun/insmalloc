#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include "./alignment.h"
#include "./bitwise.h"

namespace ins::bit {

   enum class AcquireMode {
      FirstFit,
      BuddyFit,
      BestFit,
   };

   struct Bitmap64 {
      uint64_t usebits;

      Bitmap64(uint64_t usebits = 0)
         : usebits(usebits) {
      }
      bool isFull() {
         return this->usebits == uint64_t(-1);
      }
      bool isEmpty() {
         return this->usebits == 0;
      }
      void clean() {
         this->usebits = 0;
      }
      size_t count() {
         size_t c = 0;
         for (uint64_t n = this->usebits; n; n &= (n - 1)) c++;
         return c;
      }
      void print();
      std::string str();
   };

   struct AlignedHierarchyBitmap64 : Bitmap64 {
      static const uint64_t cAlignedSelectionMask[7];
      int8_t spareSizeL2;

      AlignedHierarchyBitmap64(uint64_t usebits = 0, int8_t spareSizeL2 = 5)
         : Bitmap64(usebits), spareSizeL2(spareSizeL2) {
      }

      void clean() {
         this->usebits = 0;
         this->spareSizeL2 = 5;
      }

      intptr_t getSpareSizeL2() {
         int sizeL2;
         uint64_t spreadMap = ~this->usebits;
         for (sizeL2 = 0; spreadMap & cAlignedSelectionMask[sizeL2]; sizeL2++) {
            spreadMap &= spreadMap >> (1 << sizeL2);
         }
         return sizeL2 - 1;
      }

      // computeAvailabiltyMap: Spread free bit to the master bit for the given size
      uint64_t computeAvailabiltyMap(size_t sizeL2) {
         uint64_t spreadMap = ~this->usebits;
         switch (sizeL2) {
         case 6: spreadMap &= spreadMap >> 32;
         case 5: spreadMap &= spreadMap >> 16;
         case 4: spreadMap &= spreadMap >> 8;
         case 3: spreadMap &= spreadMap >> 4;
         case 2: spreadMap &= spreadMap >> 2;
         case 1: spreadMap &= spreadMap >> 1;
            return spreadMap & cAlignedSelectionMask[sizeL2];
         case 0:
            return spreadMap;
         default:
            return 0;
         };
      }

      // acquireFromSelectionMap: Match selection map to have a free index
      intptr_t acquireFromSelectionMap(uint64_t selectionMap, size_t size) {
         intptr_t index = lsb_64(selectionMap);
         uint64_t sizeMask = (uint64_t(1) << size) - 1; // note: cpu bug with size of 64
         this->usebits |= sizeMask << index;
         return index;
      }

      template <AcquireMode mode>
      intptr_t acquire(size_t sizeL2);

      // acquire <FirstFit>: Fit selection map based on the acquisition mode
      template <>
      intptr_t acquire<AcquireMode::FirstFit>(size_t sizeL2) {
         if (uint64_t selectionMap = this->computeAvailabiltyMap(sizeL2)) {
            return this->acquireFromSelectionMap(selectionMap, size_t(1) << sizeL2);
         }
         else {
            if (this->spareSizeL2 >= sizeL2) this->spareSizeL2 = sizeL2 - 1;
            return -1;
         }
      }

      // acquire <BuddyFit>: Prune upper selection map bits to priorize splitted span
      template <>
      intptr_t acquire<AcquireMode::BuddyFit>(size_t sizeL2) {
         if (uint64_t selectionMap = this->computeAvailabiltyMap(sizeL2)) {
            auto size = size_t(1) << sizeL2;
            if (uint64_t upperMap = (selectionMap & (selectionMap >> size)) & cAlignedSelectionMask[sizeL2 + 1]) {
               uint64_t notBuddyMap = upperMap | (upperMap << size); // Exclude upper full span
               selectionMap = selectionMap ^ notBuddyMap;
               if (!selectionMap) selectionMap = upperMap;
            }
            return this->acquireFromSelectionMap(selectionMap, size_t(1) << sizeL2);
         }
         else {
            if (this->spareSizeL2 >= sizeL2) this->spareSizeL2 = sizeL2 - 1;
            return -1;
         }
      }

      // acquire <BestFit>: Prune upper selection map bits to priorize smallest span splitting
      template <>
      intptr_t acquire<AcquireMode::BestFit>(size_t sizeL2) {
         if (uint64_t selectionMap = this->computeAvailabiltyMap(sizeL2)) {
            auto size = size_t(1) << sizeL2;
            size_t upperShift = size;
            for (;;) {
               sizeL2++;
               if (uint64_t upperMap = (selectionMap & (selectionMap >> upperShift)) & cAlignedSelectionMask[sizeL2]) {
                  uint64_t notBuddyMap = upperMap | (upperMap << upperShift);
                  if (uint64_t fittedMap = selectionMap ^ notBuddyMap) {
                     return this->acquireFromSelectionMap(fittedMap, size);
                  }
                  selectionMap = upperMap;
                  upperShift <<= 1;
               }
               else {
                  if (this->spareSizeL2 >= sizeL2) this->spareSizeL2 = sizeL2 - 1;
                  return this->acquireFromSelectionMap(selectionMap, size);
               }
            }
         }
         else {
            if (this->spareSizeL2 >= sizeL2) this->spareSizeL2 = sizeL2 - 1;
            return -1;
         }
      }

      void release(int index, size_t sizeL2) {
         size_t size = size_t(1) << sizeL2;
         uint64_t sizeMask = (uint64_t(1) << size) - 1; // note: cpu bug with size of 64
         this->usebits &= ~(sizeMask << index);
      }
      void print();
   };

   struct BinaryHierarchyBitmap64 : Bitmap64 {

      BinaryHierarchyBitmap64(uint64_t usebits = 0)
         : Bitmap64(usebits) {
      }

      void clean() {
         this->usebits = 0;
      }

      intptr_t getSpareSizeL2() {
         if (this->usebits == 0) return 6;
#if 1
         if (uint64_t availabilityMap = ~this->usebits) {
            if (availabilityMap &= availabilityMap >> 1) {
               if (availabilityMap &= availabilityMap >> 2) {
                  if (availabilityMap &= availabilityMap >> 4) {
                     if (availabilityMap &= availabilityMap >> 8) {
                        if (availabilityMap &= availabilityMap >> 16) return 5;
                        else return 4;
                     }
                     else return 3;
                  }
                  else return 2;
               }
               else return 1;
            }
            else return 0;
         }
         else return -1;
#else
         uint64_t availabilityMap = ~this->usebits; if (!availabilityMap) return -1;
         availabilityMap &= availabilityMap >> 1; if (!availabilityMap) return 0;
         availabilityMap &= availabilityMap >> 2; if (!availabilityMap) return 1;
         availabilityMap &= availabilityMap >> 4; if (!availabilityMap) return 2;
         availabilityMap &= availabilityMap >> 8; if (!availabilityMap) return 3;
         availabilityMap &= availabilityMap >> 16; if (!availabilityMap) return 4;
         return 5;
#endif
      }
      // computeAvailabiltyMap: Spread free bit to the master bit for the given size
      uint64_t computeAvailabiltyMap(size_t sizeL2) {
         uint64_t availabilityMap = ~this->usebits;
         switch (sizeL2) {
         case 6: availabilityMap &= availabilityMap >> 32;
         case 5: availabilityMap &= availabilityMap >> 16;
         case 4: availabilityMap &= availabilityMap >> 8;
         case 3: availabilityMap &= availabilityMap >> 4;
         case 2: availabilityMap &= availabilityMap >> 2;
         case 1: availabilityMap &= availabilityMap >> 1;
         case 0: return availabilityMap;
         default: return 0;
         };
      }

      // acquireFromSelectionMap: Match selection map to have a free index
      intptr_t acquireFromSelectionMap(uint64_t selectionMap, size_t size) {
         intptr_t index = lsb_64(selectionMap);
         uint64_t sizeMask = (uint64_t(1) << size) - 1; // note: cpu bug with size of 64
         this->usebits |= sizeMask << index;
         return index;
      }

      template <AcquireMode mode>
      intptr_t acquire(size_t sizeL2);

      // acquire <FirstFit>: Fit selection map based on the acquisition mode
      template <>
      intptr_t acquire<AcquireMode::FirstFit>(size_t sizeL2) {
         if (uint64_t availabilityMap = this->computeAvailabiltyMap(sizeL2)) {
            uint64_t selectionMap = availabilityMap & ~(availabilityMap << 1);
            return this->acquireFromSelectionMap(selectionMap, size_t(1) << sizeL2);
         }
         else {
            return -1;
         }
      }

      intptr_t adjustAvailabilityMap(size_t sizeL2, uint64_t availabilityMap) {
         uint64_t selectionMap = availabilityMap & ~(availabilityMap << 1);
#if 0
         switch (sizeL2) {
         case 0:
            if (availabilityMap &= (availabilityMap >> 1)) {
               if (uint64_t fittedMap = selectionMap & ~availabilityMap) return fittedMap;
         case 1:
            if (availabilityMap &= (availabilityMap >> 2)) {
               if (uint64_t fittedMap = selectionMap & ~availabilityMap) return fittedMap;
         case 2:
            if (availabilityMap &= (availabilityMap >> 4)) {
               if (uint64_t fittedMap = selectionMap & ~availabilityMap) return fittedMap;
         case 3:
            if (availabilityMap &= (availabilityMap >> 8)) {
               if (uint64_t fittedMap = selectionMap & ~availabilityMap) return fittedMap;
         case 4:
            if (availabilityMap &= (availabilityMap >> 16)) {
               if (uint64_t fittedMap = selectionMap & ~availabilityMap) return fittedMap;
         case 5:
            if (availabilityMap &= (availabilityMap >> 32)) {
               if (uint64_t fittedMap = selectionMap & ~availabilityMap) return fittedMap;
            }
            }
            }
            }
            }
            }
         default:
            return selectionMap;
         }
#else
         for (size_t size = size_t(1) << sizeL2;; size <<= 1) {
            if (availabilityMap &= (availabilityMap >> size)) {
               if (uint64_t fittedMap = selectionMap & ~availabilityMap) return fittedMap;
            }
            else return selectionMap;
         }
#endif
      }

      // acquire <BestFit>: Prune upper selection map bits to priorize smallest span splitting
      template <>
      intptr_t acquire<AcquireMode::BestFit>(size_t sizeL2) {
         if (this->usebits == 0) {
            return this->acquireFromSelectionMap(1, size_t(1) << sizeL2);
         }
         else if (uint64_t availabilityMap = this->computeAvailabiltyMap(sizeL2)) {
            uint64_t selectionMap = this->adjustAvailabilityMap(sizeL2, availabilityMap);
            return this->acquireFromSelectionMap(selectionMap, size_t(1) << sizeL2);
         }
         else {
            return -1;
         }
      }

      void release(int index, size_t sizeL2) {
         size_t size = size_t(1) << sizeL2;
         uint64_t sizeMask = (uint64_t(1) << size) - 1; // note: cpu bug with size of 64
         this->usebits &= ~(sizeMask << index);
      }
      void print();
   };

   struct UniformBitmap64 : Bitmap64 {
      int acquire() {
         if (this->usebits != -1) return -1;
         int index = lsb_64(this->usebits);
         this->usebits |= uint64_t(1) << index;
         return index;
      }
      void release(int index) {
         this->usebits &= ~(uint64_t(1) << index);
      }
   };

}
