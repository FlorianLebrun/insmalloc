#include <ins/memory/regions.h>

using namespace ins;

const mem::tRegionSizingInfos mem::cst::RegionSizingInfos[cst::RegionSizingCount] = {
/*sizeL2=0*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=1*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=2*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=3*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=4*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=5*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=6*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=7*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=8*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=9*/ {/*granularityL2*/0, /*pageSizeL2*/0, /*layouts{retention,committedPages,committeSize}*/{{0, 0, 0, }, }, },
/*sizeL2=10*/ {/*granularityL2*/10, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 1, 65536, }, }, },
/*sizeL2=11*/ {/*granularityL2*/11, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 1, 65536, }, }, },
/*sizeL2=12*/ {/*granularityL2*/12, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 1, 65536, }, }, },
/*sizeL2=13*/ {/*granularityL2*/13, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 1, 65536, }, }, },
/*sizeL2=14*/ {/*granularityL2*/14, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 1, 65536, }, }, },
/*sizeL2=15*/ {/*granularityL2*/15, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 1, 65536, }, }, },
/*sizeL2=16*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 1, 65536, }, }, },
/*sizeL2=17*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 2, 131072, }, }, },
/*sizeL2=18*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 4, 262144, }, }, },
/*sizeL2=19*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{256, 8, 524288, }, {32, 5, 327680, }, }, },
/*sizeL2=20*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 16, 1048576, }, }, },
/*sizeL2=21*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 32, 2097152, }, }, },
/*sizeL2=22*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 64, 4194304, }, }, },
/*sizeL2=23*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 128, 8388608, }, }, },
/*sizeL2=24*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 256, 16777216, }, }, },
/*sizeL2=25*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 512, 33554432, }, }, },
/*sizeL2=26*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 1024, 67108864, }, }, },
/*sizeL2=27*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 2048, 134217728, }, }, },
/*sizeL2=28*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 4096, 268435456, }, }, },
/*sizeL2=29*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 8192, 536870912, }, }, },
/*sizeL2=30*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 16384, 1073741824, }, }, },
/*sizeL2=31*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 32768, 2147483648, }, }, },
/*sizeL2=32*/ {/*granularityL2*/16, /*pageSizeL2*/16, /*layouts{retention,committedPages,committeSize}*/{{0, 65536, 4294967296, }, }, },
};

const uint32_t mem::cst::RegionMasks[cst::RegionSizingCount] = {
0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 
0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff, 0x1ffff, 0x3ffff, 0x7ffff, 0xfffff, 0x1fffff, 0x3fffff, 0x7fffff, 
0xffffff, 0x1ffffff, 0x3ffffff, 0x7ffffff, 0xfffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff, 0xffffffff, 
};

