#include <ins/memory/structs.h>
#include <stdio.h>

using namespace ins;
using namespace ins::mem;

mem::sz2a::sz2a(size_t size) {
   if (size < 1000) set(size, 1, "Byte");
   else if (size < 1000000) set(size, 1000, "Ko");
   else if (size < 1000000000) set(size, 1000000, "Mo");
   else set(size, 1000000000, "Go");
}

void mem::sz2a::set(size_t size, size_t factor, const char* unit) {
   auto sz = floor(double(size) * 100.0 / double(factor)) / 100.0;
   sprintf_s(chars, sizeof(chars), "%llg %s", sz, unit);
}
