#include <ins/memory/objects.h>

namespace ins {

const size_t ObjectLayoutCount = 80;

const size_t LayoutRangeSizeCount = 512;
const size_t SmallSizeLimit = LayoutRangeSizeCount << 3;
const size_t MediumSizeLimit = SmallSizeLimit << 4;
const size_t LargeSizeLimit = MediumSizeLimit << 4;

struct tLayoutRangeBin {uint8_t layoutMin = 0, layoutMax = 0;};
extern const uint8_t small_object_layouts[LayoutRangeSizeCount + 1];
extern const tLayoutRangeBin medium_object_layouts[LayoutRangeSizeCount];
extern const tLayoutRangeBin large_object_layouts[LayoutRangeSizeCount];
}
