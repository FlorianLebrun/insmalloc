
#include <ins/memory/configuration.h>
#include "./blocks.h"

using namespace ins;

const ins::PageLayout ins::cPageLayouts[48] = {
   { 0, 0 },
   { 0, 4194305 },
   { 0, 2097153 },
   { 0, 1398102 },
   { 0, 1048577 },
   { 0, 838861 },
   { 0, 699051 },
   { 0, 599187 },
   { 0, 524289 },
   { 0, 419431 },
   { 0, 349526 },
   { 0, 299594 },
   { 0, 262145 },
   { 0, 209716 },
   { 0, 174763 },
   { 0, 149797 },
   { 0, 131073 },
   { 0, 104858 },
   { 0, 87382 },
   { 0, 74899 },
   { 0, 65537 },
   { 0, 52429 },
   { 65536, 52429 },
   { 131072, 52429 },
   { 196608, 52429 },
   { 262144, 52429 },
   { 0, 43691 },
   { 65536, 43691 },
   { 131072, 43691 },
   { 0, 37450 },
   { 65536, 37450 },
   { 131072, 37450 },
   { 196608, 37450 },
   { 262144, 37450 },
   { 327680, 37450 },
   { 393216, 37450 },
   { 0, 13108 },
   { 65536, 13108 },
   { 131072, 13108 },
   { 196608, 13108 },
   { 262144, 13108 },
   { 0, 9363 },
   { 65536, 9363 },
   { 131072, 9363 },
   { 196608, 9363 },
   { 262144, 9363 },
   { 327680, 9363 },
   { 393216, 9363 }
};

static ins::SlabPnS1Class slab_0(0, 0, 1, 1, 10);
static ins::SlabPnS1Class slab_1(1, 1, 2, 1, 11);
static ins::SlabPnS1Class slab_2(2, 2, 3, 3, 10);
static ins::SlabPnS1Class slab_3(3, 3, 4, 1, 12);
static ins::SlabPnS1Class slab_4(4, 4, 5, 5, 10);
static ins::SlabPnS1Class slab_5(5, 5, 6, 3, 11);
static ins::SlabPnS1Class slab_6(6, 6, 7, 7, 10);
static ins::SlabPnS1Class slab_7(7, 7, 8, 1, 13);
static ins::SlabPnS1Class slab_8(8, 8, 9, 5, 11);
static ins::SlabPnS1Class slab_9(9, 9, 10, 3, 12);
static ins::SlabPnS1Class slab_10(10, 10, 11, 7, 11);
static ins::SlabPnS1Class slab_11(11, 11, 12, 1, 14);
static ins::SlabPnS1Class slab_12(12, 12, 13, 5, 12);
static ins::SlabPnS1Class slab_13(13, 13, 14, 3, 13);
static ins::SlabPnS1Class slab_14(14, 14, 15, 7, 12);
static ins::SlabPnS1Class slab_15(15, 15, 16, 1, 15);
static ins::SlabPnS1Class slab_16(16, 16, 17, 5, 13);
static ins::SlabPnS1Class slab_17(17, 17, 18, 3, 14);
static ins::SlabPnS1Class slab_18(18, 18, 19, 7, 13);
static ins::SlabP1SnClass slab_19(19, 1, 16, {20});
static ins::SlabPnSnClass slab_20(20, 19, 5, 14, {21,22,23,24,25});
static ins::SlabPnSnClass slab_21(21, 20, 3, 15, {26,27,28});
static ins::SlabPnSnClass slab_22(22, 21, 7, 14, {29,30,31,32,33,34,35});
static ins::SlabP1SnClass slab_23(23, 5, 16, {36,37,38,39,40});
static ins::SlabP1SnClass slab_24(24, 7, 16, {41,42,43,44,45,46,47});

ins::SlabClass* ins::cSlabBinTable[22] = {
   &slab_0, &slab_1, &slab_2, &slab_3, &slab_4, &slab_5, &slab_6, &slab_7,
   &slab_8, &slab_9, &slab_10, &slab_11, &slab_12, &slab_13, &slab_14, &slab_15,
   &slab_16, &slab_17, &slab_18, &slab_20, &slab_21, &slab_22
};

ins::SlabClass* ins::cSlabClassTable[25] = {
   &slab_0, &slab_1, &slab_2, &slab_3, &slab_4, &slab_5, &slab_6, &slab_7,
   &slab_8, &slab_9, &slab_10, &slab_11, &slab_12, &slab_13, &slab_14, &slab_15,
   &slab_16, &slab_17, &slab_18, &slab_19, &slab_20, &slab_21, &slab_22, &slab_23,
   &slab_24
};

static ins::BlockPnS1Class block_0(0, 0, 1, 4, &slab_0); // sizeof 16
static ins::BlockPnS1Class block_1(1, 1, 1, 5, &slab_1); // sizeof 32
static ins::BlockPnS1Class block_2(2, 2, 3, 4, &slab_2); // sizeof 48
static ins::BlockPnS1Class block_3(3, 3, 1, 6, &slab_3); // sizeof 64
static ins::BlockPnS1Class block_4(4, 4, 5, 4, &slab_4); // sizeof 80
static ins::BlockPnS1Class block_5(5, 5, 3, 5, &slab_5); // sizeof 96
static ins::BlockPnS1Class block_6(6, 6, 7, 4, &slab_6); // sizeof 112
static ins::BlockPnS1Class block_7(7, 7, 1, 7, &slab_7); // sizeof 128
static ins::BlockPnS1Class block_8(8, 8, 5, 5, &slab_8); // sizeof 160
static ins::BlockPnS1Class block_9(9, 9, 3, 6, &slab_9); // sizeof 192
static ins::BlockPnS1Class block_10(10, 10, 7, 5, &slab_10); // sizeof 224
static ins::BlockPnS1Class block_11(11, 11, 1, 8, &slab_11); // sizeof 256
static ins::BlockPnS1Class block_12(12, 12, 5, 6, &slab_12); // sizeof 320
static ins::BlockPnS1Class block_13(13, 13, 3, 7, &slab_13); // sizeof 384
static ins::BlockPnS1Class block_14(14, 14, 7, 6, &slab_14); // sizeof 448
static ins::BlockPnS1Class block_15(15, 15, 1, 9, &slab_15); // sizeof 512
static ins::BlockPnS1Class block_16(16, 16, 5, 7, &slab_16); // sizeof 640
static ins::BlockPnS1Class block_17(17, 17, 3, 8, &slab_17); // sizeof 768
static ins::BlockPnS1Class block_18(18, 18, 7, 7, &slab_18); // sizeof 896
static ins::BlockP1SnClass block_19(19, 19, 1, 10, 6, &slab_19); // sizeof 1024
static ins::BlockPnSnClass block_20(20, 20, 5, 8, 6, &slab_20); // sizeof 1280
static ins::BlockPnSnClass block_21(21, 21, 3, 9, 6, &slab_21); // sizeof 1536
static ins::BlockPnSnClass block_22(22, 22, 7, 8, 6, &slab_22); // sizeof 1792
static ins::BlockP1SnClass block_23(23, 23, 1, 11, 5, &slab_19); // sizeof 2048
static ins::BlockPnSnClass block_24(24, 24, 5, 9, 5, &slab_20); // sizeof 2560
static ins::BlockPnSnClass block_25(25, 25, 3, 10, 5, &slab_21); // sizeof 3072
static ins::BlockPnSnClass block_26(26, 26, 7, 9, 5, &slab_22); // sizeof 3584
static ins::BlockP1SnClass block_27(27, 27, 1, 12, 4, &slab_19); // sizeof 4096
static ins::BlockPnSnClass block_28(28, 28, 5, 10, 4, &slab_20); // sizeof 5120
static ins::BlockPnSnClass block_29(29, 29, 3, 11, 4, &slab_21); // sizeof 6144
static ins::BlockPnSnClass block_30(30, 30, 7, 10, 4, &slab_22); // sizeof 7168
static ins::BlockP1SnClass block_31(31, 31, 1, 13, 3, &slab_19); // sizeof 8192
static ins::BlockPnSnClass block_32(32, 32, 5, 11, 3, &slab_20); // sizeof 10240
static ins::BlockPnSnClass block_33(33, 33, 3, 12, 3, &slab_21); // sizeof 12288
static ins::BlockPnSnClass block_34(34, 34, 7, 11, 3, &slab_22); // sizeof 14336
static ins::BlockP1SnClass block_35(35, 35, 1, 14, 2, &slab_19); // sizeof 16384
static ins::BlockPnSnClass block_36(36, 36, 5, 12, 2, &slab_20); // sizeof 20480
static ins::BlockPnSnClass block_37(37, 37, 3, 13, 2, &slab_21); // sizeof 24576
static ins::BlockPnSnClass block_38(38, 38, 7, 12, 2, &slab_22); // sizeof 28672
static ins::BlockP1SnClass block_39(39, 39, 1, 15, 1, &slab_19); // sizeof 32768
static ins::BlockPnSnClass block_40(40, 40, 5, 13, 1, &slab_20); // sizeof 40960
static ins::BlockPnSnClass block_41(41, 41, 3, 14, 1, &slab_21); // sizeof 49152
static ins::BlockPnSnClass block_42(42, 42, 7, 13, 1, &slab_22); // sizeof 57344
static ins::BlockPageSpanClass block_43(43, 1, 16); // sizeof 65536
static ins::BlockPnSnClass block_44(44, 43, 5, 14, 0, &slab_20); // sizeof 81920
static ins::BlockPnSnClass block_45(45, 44, 3, 15, 0, &slab_21); // sizeof 98304
static ins::BlockPnSnClass block_46(46, 45, 7, 14, 0, &slab_22); // sizeof 114688
static ins::BlockPageSpanClass block_47(47, 1, 17); // sizeof 131072
static ins::BlockP1SnClass block_48(48, 46, 5, 15, 1, &slab_23); // sizeof 163840
static ins::BlockPageSpanClass block_49(49, 3, 16); // sizeof 196608
static ins::BlockP1SnClass block_50(50, 47, 7, 15, 1, &slab_24); // sizeof 229376
static ins::BlockPageSpanClass block_51(51, 1, 18); // sizeof 262144
static ins::BlockPageSpanClass block_52(52, 5, 16); // sizeof 327680
static ins::BlockPageSpanClass block_53(53, 3, 17); // sizeof 393216
static ins::BlockPageSpanClass block_54(54, 7, 16); // sizeof 458752
static ins::BlockPageSpanClass block_55(55, 1, 19); // sizeof 524288
static ins::BlockPageSpanClass block_56(56, 5, 17); // sizeof 655360
static ins::BlockPageSpanClass block_57(57, 3, 18); // sizeof 786432
static ins::BlockPageSpanClass block_58(58, 7, 17); // sizeof 917504
static ins::BlockPageSpanClass block_59(59, 1, 20); // sizeof 1048576
static ins::BlockPageSpanClass block_60(60, 5, 18); // sizeof 1310720
static ins::BlockPageSpanClass block_61(61, 3, 19); // sizeof 1572864
static ins::BlockPageSpanClass block_62(62, 7, 18); // sizeof 1835008
static ins::BlockPageSpanClass block_63(63, 1, 21); // sizeof 2097152
static ins::BlockPageSpanClass block_64(64, 5, 19); // sizeof 2621440
static ins::BlockPageSpanClass block_65(65, 3, 20); // sizeof 3145728
static ins::BlockPageSpanClass block_66(66, 7, 19); // sizeof 3670016
static ins::BlockUnitSpanClass block_67(67); // sizeof -1
static ins::BlockPageSpanClass block_68(68, 5, 20); // sizeof 5242880
static ins::BlockPageSpanClass block_69(69, 3, 21); // sizeof 6291456
static ins::BlockPageSpanClass block_70(70, 7, 20); // sizeof 7340032
static ins::BlockPageSpanClass block_71(71, 5, 21); // sizeof 10485760
static ins::BlockPageSpanClass block_72(72, 7, 21); // sizeof 14680064

ins::BlockClass* ins::cBlockBinTable[48] = {
   &block_0, &block_1, &block_2, &block_3, &block_4, &block_5, &block_6, &block_7,
   &block_8, &block_9, &block_10, &block_11, &block_12, &block_13, &block_14, &block_15,
   &block_16, &block_17, &block_18, &block_19, &block_20, &block_21, &block_22, &block_23,
   &block_24, &block_25, &block_26, &block_27, &block_28, &block_29, &block_30, &block_31,
   &block_32, &block_33, &block_34, &block_35, &block_36, &block_37, &block_38, &block_39,
   &block_40, &block_41, &block_42, &block_44, &block_45, &block_46, &block_48, &block_50
};
 
ins::BlockClass* ins::cBlockClassTable[73] = {
   &block_0, &block_1, &block_2, &block_3, &block_4, &block_5, &block_6, &block_7,
   &block_8, &block_9, &block_10, &block_11, &block_12, &block_13, &block_14, &block_15,
   &block_16, &block_17, &block_18, &block_19, &block_20, &block_21, &block_22, &block_23,
   &block_24, &block_25, &block_26, &block_27, &block_28, &block_29, &block_30, &block_31,
   &block_32, &block_33, &block_34, &block_35, &block_36, &block_37, &block_38, &block_39,
   &block_40, &block_41, &block_42, &block_43, &block_44, &block_45, &block_46, &block_47,
   &block_48, &block_49, &block_50, &block_51, &block_52, &block_53, &block_54, &block_55,
   &block_56, &block_57, &block_58, &block_59, &block_60, &block_61, &block_62, &block_63,
   &block_64, &block_65, &block_66, &block_67, &block_68, &block_69, &block_70, &block_71,
   &block_72
};

BlockClass* ins::getBlockClass(size_target_t target) {

    static uint8_t block_sizes_map[24][4] = {
        { 0, 0, 0, 0 },
        { 0, 0, 0, 0 },
        { 0, 0, 1, 1 },
        { 0, 1, 2, 3 },
        { 0, 2, 4, 6 },
        { 1, 5, 8, 10 },
        { 3, 9, 12, 14 },
        { 7, 13, 16, 18 },
        { 11, 17, 20, 22 },
        { 15, 21, 24, 26 },
        { 19, 25, 28, 30 },
        { 23, 29, 32, 34 },
        { 27, 33, 36, 38 },
        { 31, 37, 40, 42 },
        { 35, 41, 44, 46 },
        { 39, 45, 48, 50 },
        { 43, 49, 52, 54 },
        { 47, 53, 56, 58 },
        { 51, 57, 60, 62 },
        { 55, 61, 64, 66 },
        { 59, 65, 68, 70 },
        { 63, 69, 71, 72 },
        { 67, 67, 67, 67 },
        { 71, 67, 67, 67 }
    };
    
    auto id = block_sizes_map[target.shift][target.packing >> 1];
    return ins::cBlockClassTable[id];
}
