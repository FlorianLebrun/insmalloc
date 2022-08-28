#include <ins/binary/alignment.h>
#include <ins/memory/space.h>
#include <ins/memory/structs.h>
#include <stdio.h>
#include <vector>
#include <fstream>
#include <iostream>

extern void test_descriptor_region();
void test_small_object();

using namespace ins;

void generate_layout_config(std::string path);

int main() {
   auto div = getBlockDivider(64);
   uint32_t p = 0x12560000 + getBlockIndexToOffset(64, 0, 0xffff);
   uint32_t i1 = getBlockOffsetToIndex(div, p - 64, 0xffff);
   uint32_t i2 = getBlockOffsetToIndex(div, p - 65, 0xffff);


   //generate_layout_config("C:/git/project/inslick.malloc/lib/ins.memory.space2");
   test_small_object();
   //test_descriptor_region();

   return 0;
}

