#include <sat/memory/memory.hpp>
#include <vector>


void test_simple() {
   std::vector<size_t> segs;
   segs.push_back(sat::memory::allocSegmentSpan(1));
   segs.push_back(sat::memory::allocSegmentSpan(10));
   segs.push_back(sat::memory::allocSegmentSpan(100));
   sat::memory::freeSegmentSpan(segs[1], 10);
   segs.push_back(sat::memory::allocSegmentSpan(1));
   sat::memory::table.print();
}

int main() {
   sat::memory::table.initialize();

   test_simple();

   return 0;
}
