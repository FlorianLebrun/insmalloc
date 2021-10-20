#pragma once
#include <sat/memory/descriptors.h>

namespace sat {

    struct PageLayout;
    struct BlockClass;
    struct SlabClass;
 
    static const int cPageLayoutsCount = 48;
    extern const PageLayout cPageLayouts[48];

    static const int cBlockBinCount = 48;
    extern BlockClass* cBlockBinTable[48];
    static const int cBlockClassCount = 73;
    extern BlockClass* cBlockClassTable[73];

    static const int cSlabBinCount = 22;
    extern SlabClass* cSlabBinTable[22];
    static const int cSlabClassCount = 25;
    extern SlabClass* cSlabClassTable[25];

    BlockClass* getBlockClass(size_target_t target);
}
