#include <sat/system-object.hpp>
#include <sat/memory.hpp>

using namespace sat;

int main() {
   sat::MemoryTableController::self.initialize();
   auto sizeId = memory::getSystemSizeID(200);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   memory::allocSystemBuffer(sizeId);
   MemoryTableController::self.printSegments();
   return 0;
}
