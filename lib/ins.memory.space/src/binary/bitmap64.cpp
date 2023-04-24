#include <ins/binary/bitmap64.h>
#include <stdio.h>

using namespace ins;

uint64_t g_maxSpreadMap = 0;

void bit::Bitmap64::print() {
   printf("bitmap%s\n", this->str().c_str());
}

std::string bit::Bitmap64::str() {
   char bitchars[64];
   for (int i = 0; i < 64; i++) {
      bitchars[i] = ((this->usebits >> i) & 1) ? '1' : '.';
   }
   char text[128];
   sprintf(text, "[%.64s] 0x%.16llX", bitchars, this->usebits);
   return text;
}

const uint64_t bit::AlignedHierarchyBitmap64::cAlignedSelectionMask[7] = {
   0xffffffffffffffff, 0x5555555555555555, 0x1111111111111111, 0x0101010101010101,
   0x0001000100010001, 0x0000000100000001, 0x0000000000000001,
};

void bit::AlignedHierarchyBitmap64::print() {
   auto eval_spare_size = this->getSpareSizeL2() < 0 ? -1 : 1 << this->getSpareSizeL2();
   auto spare_size = this->spareSizeL2 < 0 ? -1 : 1 << this->spareSizeL2;
   printf("ABH-map%s | spare-size = %d(%d)\n", this->str().c_str(), spare_size, eval_spare_size);
}

void bit::BinaryHierarchyBitmap64::print() {
   auto spare_size = this->getSpareSizeL2() < 0 ? -1 : 1 << this->getSpareSizeL2();
   printf("BH-map%s | spare-size = %d\n", this->str().c_str(), spare_size);
}
