#include <sat/system-object.hpp>
#include <sat/memory.hpp>

using namespace sat;

int main() {
   auto sizeId = memory::getSystemSizeID(200);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   memory::table->printSegments();
   return 0;
}
