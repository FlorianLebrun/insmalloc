
namespace sat {

   struct MemorySpace;
   struct MemoryRegion32;
   struct MemoryUnit;

   struct MemoryUnit {
      MemoryUnit* linknext;

      int16_t position;
      int16_t width = 0; // span first entry is length-1, last is -width of first entry, otherwise 0

      uint8_t regionID;
      union {
         struct {

            // Memory usage
            uint8_t isUsed : 1;
            uint8_t isReserved : 1;
            uint8_t isForbidden : 1;
            uint8_t is_misclassed : 1;

            // Other infos
            uint8_t isFragmented : 1;
            uint8_t isFrontier : 1;
         };
         uint8_t options = 0;
      };

      int8_t fragments_spare_metric = 0;
      BinaryHierarchyBitmap64 fragments_bitmap;

      uint64_t commited = 0;

      uintptr_t address();

      void commitMemorySpan();

      void commitMemory();
      bool reserveMemory();
      void decommitMemory();
      void releaseMemory();

      void setWidth(uint16_t width) {
         this[0].width = width;
         this[width].width = -width;
      }
   };
   static_assert(sizeof(MemoryUnit) <= 64, "bad size");

   struct MemoryRegion32 {
      uint8_t regionID = 0;
      uintptr_t address = 0;

      MemoryUnit units_table[cUnitPerRegion];
      PageEntry pages_table[cPagePerRegion];

      MemoryRegion32(uint8_t regionID, bool restrictSmallPtrZone = true);
      ~MemoryRegion32();

      // Unit aligned allocation
      MemoryUnit* acquireUnitSpan(size_t length);
      void releaseUnitSpan(MemoryUnit* unit, size_t length);
      bool analyzeUnitFreeSpan(size_t index);

      // Helpers
      void print();
      void check();
   };

   struct MemoryPageSpanManifold {

      static const auto cBinPerManifold = cPagePerUnitL2 + 1;
      static const auto cBinEmptySizeId = cPagePerUnitL2;

      struct PageSpanBin {
         int32_t unseenUnitCount = 0;
         MemoryUnit* units = 0;
      };

      PageSpanBin fullslabs;
      PageSpanBin bins[cBinPerManifold];

      uint8_t packing;
      size_t garbage_count = 0;
      std::mutex lock;

      address_t acquirePageSpan(MemorySpace* space, size_t lengthL2);
      void releasePageSpan(MemorySpace* space, address_t adress, size_t lengthL2);
      void scavengeCaches(MemorySpace* space);

   };
}