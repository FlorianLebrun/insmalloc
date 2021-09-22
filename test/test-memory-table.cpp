#include <sat/memory/space.h>
#include "./utils.h"

#include <vector>
#include <functional>
#include <atomic>
#include <mimalloc.h>

using namespace sat;

extern"C" uint64_t test_value;
uint64_t test_value = 0;

void test_buddy_bitmap() {
#define _ASSERT(x)
   BinaryHierarchyBitmap64 b;
   static const auto mode = AcquireMode::BestFit;

   // Test with 64 span size
   if (0) {
      printf("\n-------------- Test with 64 span size --------------\n");
      b.acquire<mode>(6);
      b.print();
   }

   // Test with multiple combination
   if (1) {
      printf("\n-------------- Test multiple combination --------------\n");
      std::vector<int> s;

      b.usebits = 0xFFFFFFFF00FF0000;
      b.print();
      b.acquire<mode>(2);
      b.print();
      _ASSERT(b.usebits == 0xFFFFFFFF00FF0FFF);

      b.clean();
      s.push_back(b.acquire<mode>(2));
      s.push_back(b.acquire<mode>(2));
      s.push_back(b.acquire<mode>(2));
      s.push_back(b.acquire<mode>(2));
      b.print();
      _ASSERT(b.usebits == 0x000000000000FFFF);

      b.release(s[0], 2);
      b.release(s[1], 2);
      b.release(s[2], 2);
      b.print();
      _ASSERT(b.usebits == 0x000000000000F000);

      s.push_back(b.acquire<mode>(2));
      b.print();
      _ASSERT(b.usebits == 0x000000000000FF00);

      s.push_back(b.acquire<mode>(0));
      s.push_back(b.acquire<mode>(1));
      s.push_back(b.acquire<mode>(3));
      b.print();
      _ASSERT(b.usebits == 0x0000000000FFFF0D);

      s.push_back(b.acquire<mode>(0));
      s.push_back(b.acquire<mode>(5));
      b.print();
      _ASSERT(b.usebits == 0xFFFFFFFF00FFFF0F);

      b.release(s[3], 2);
      b.print();
      _ASSERT(b.usebits == 0xFFFFFFFF00FF0F0F);

   }

   // Test fill all by one
   if (0) {
      printf("\n-------------- Test fill all by one --------------\n");
      for (int i = 0; i < 16; i++) {
         b.usebits |= uint64_t(1) << (rand() % 64);
      }
      b.print();
      for (int i = 0; i < 64; i++) {
         b.acquire<mode>(1);
         b.print();
      }
   }

   // Test performance in worst case
   if (1) {
      printf("\n-------------- Test performance --------------\n");
      int count = 20000000;
      Chrono c;

      // In worst case
      b.clean();
      c.Start();
      for (int i = 0; i < count; i++) {
         test_value = b.acquire<mode>(0);
         b.release(test_value, 0);
      }
      printf("[worst case] time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));

      // In best case
      b.clean();
      c.Start();
      b.acquire<mode>(0);
      for (int i = 0; i < count; i++) {
         test_value = b.acquire<mode>(0);
         b.release(test_value, 0);
      }
      printf("[best case] time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));

      // In mean case
      b.clean();
      c.Start();
      for (int i = 0; i < count; i++) {
         if (b.isFull()) b.clean();
         test_value = b.acquire<mode>(0);
      }
      printf("[mean case] time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));

      // In mean case
      c.Start();
      for (int i = 0; i < count; i++) {
         b.usebits = i;
         test_value = b.getSpareSizeL2();
      }
      printf("[max-size] time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));
   }
}

void test_unit_span_alloc() {
   MemorySpace* space = new MemorySpace();
   MemoryRegion32* region = space->regions[1];
   space->print();

   auto s1 = region->acquireUnitSpan(14);
   auto s2 = region->acquireUnitSpan(5);
   auto s3 = region->acquireUnitSpan(7);
   auto s4 = region->acquireUnitSpan(100);
   space->print();

   region->releaseUnitSpan(s2, 5);
   region->releaseUnitSpan(s3, 7);
   space->print();

   region->releaseUnitSpan(s1, 14);
   region->releaseUnitSpan(s4, 100);
   space->print();

   delete space;
}
#define TRACE_SPACE 0
void test_unit_fragment_alloc() {
   Chrono c;
   MemorySpace* space = new MemorySpace();
   space->print();

   int count = 10000;
   int ncycle = 2000;
   int ncycle_heating = 0;
   struct {
      double acquire_time_ns = 0;
      double release_time_ns = 0;
      int count = 0;
   } stats;
   size_t total_used = 0;

   struct seg_t { address_t addr = nullptr; size_t size = 0; };
   std::vector<seg_t> segs(count);
   auto& manifold = space->subunits_manifolds[0];
   for (int k = 0; k < ncycle; k++) {

      for (int i = 0; i < count; i++) {
         seg_t& s = segs[i];
         s.size = rand() % 5;
      }

      c.Start();
      for (int i = 0; i < count; i++) {
         seg_t& s = segs[i];
         s.addr = manifold.acquireSubunitSpan(space, s.size);
         total_used += size_t(1) << s.size;
#if _DEBUG
         space->check();
#endif
      }
      auto acquire_time_ns = c.GetDiffDouble(Chrono::NS);

#if TRACE_SPACE
      space->print();
#endif

      c.Start();
      for (int i = 0; i < count; i++) {
         seg_t& s = segs[i];
         manifold.releaseSubunitSpan(space, s.addr, s.size);
         s.addr = nullptr;
#if _DEBUG
         space->check();
#endif
      }
      auto release_time_ns = c.GetDiffDouble(Chrono::NS);

      // space->scavengeCaches();
#if TRACE_SPACE
      space->print();
      //space->scavengeCaches();
      //space->print();
#endif
      if (k >= ncycle_heating) {
         stats.acquire_time_ns += acquire_time_ns;
         stats.release_time_ns += release_time_ns;
         stats.count += count;
      }
   }

   printf("[subunit-span] acquire-time = %g ns\n", stats.acquire_time_ns / float(stats.count));
   printf("[subunit-span] release-time = %g ns\n", stats.release_time_ns / float(stats.count));
   printf("[subunit-span] used = %d\n", total_used / ncycle);

   delete space;
}
