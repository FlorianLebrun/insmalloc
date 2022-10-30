#pragma once
namespace ins {
   namespace cst {

      const size_t ObjectLayoutMin  = 0;
      const size_t ObjectLayoutMax = 77;
      const size_t ObjectLayoutCount = 78;

      const size_t ObjectRegionTemplateCount = 12;

      const size_t LayoutRangeSizeCount = 512;
      const size_t SmallSizeLimit = LayoutRangeSizeCount << 3;
      const size_t MediumSizeLimit = SmallSizeLimit << 4;
      const size_t LargeSizeLimit = MediumSizeLimit << 4;

      struct tLayoutRangeBin {uint8_t layoutMin = 0, layoutMax = 0;};
      extern const uint8_t small_object_layouts[LayoutRangeSizeCount + 1];
      extern const tLayoutRangeBin medium_object_layouts[LayoutRangeSizeCount];
      extern const tLayoutRangeBin large_object_layouts[LayoutRangeSizeCount];
   }
}
