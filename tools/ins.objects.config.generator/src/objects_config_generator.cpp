#include <ins/binary/alignment.h>
#include <ins/memory/space.h>
#include <ins/memory/structs.h>
#include <stdio.h>
#include <vector>
#include <fstream>
#include <iostream>

using namespace ins;

struct tObjectClass {
   uint32_t layoutID = 0;
   ObjectLayoutPolicy layoutPolicy = UncachedLargeObjectPolicy;

   size_t buffer_start = 0;
   size_t buffer_end = 0;
   size_t object_size = 0;
   size_t object_count = 0;
   size_t object_divider = 0;
   size_t slab_count = 0;
   uint32_t region_mask = -1;
   size_t region_sizeL2 = 0;
   size_t region_bytes = 0;
   size_t usable_bytes = 0;
   size_t lost_bytes = 0;
   bool selected = false;

   tObjectClass(ObjectLayoutPolicy layoutPolicy, size_t object_size) {
      this->object_size = object_size;
   }
   tObjectClass(size_t object_size, size_t page_sizeL2) : object_size(object_size), region_sizeL2(page_sizeL2) {
      this->object_divider = ins::getBlockDivider(object_size);
      this->region_mask = ((size_t(1) << this->region_sizeL2) - 1);
      this->region_bytes = size_t(1) << this->region_sizeL2;
      this->usable_bytes = this->region_bytes - cst::ObjectRegionHeadSize;

      // Approximate slab count that contains all objects containable by the available bytes
      size_t avail_size = this->usable_bytes;
      this->slab_count = 0;
      if (avail_size >= object_size) {
         do {
            this->object_count = avail_size / object_size;
            this->slab_count = ins::align<cst::ObjectPerSlab>(this->object_count) / cst::ObjectPerSlab;
            avail_size = this->usable_bytes - this->slab_count * sizeof(sObjectSlab);
         } while (avail_size < this->object_count * object_size);
         _ASSERT(this->object_count <= this->slab_count * cst::ObjectPerSlab);

         // Adjust to best fitted slab count
         for (;;) {
            this->lost_bytes = this->usable_bytes - (this->slab_count * sizeof(sObjectSlab)) - (this->object_count * object_size);
            if (this->lost_bytes >= object_size + sizeof(sObjectSlab)) {
               this->slab_count++;
               this->object_count = std::min(this->slab_count * cst::ObjectPerSlab, (this->usable_bytes - this->slab_count * sizeof(sObjectSlab)) / object_size);
            }
            else {
               break;
            }
         }
         _ASSERT(this->object_count <= this->slab_count * cst::ObjectPerSlab);
      }

      this->buffer_start = cst::ObjectRegionHeadSize + this->slab_count * sizeof(sObjectSlab);
      this->buffer_end = buffer_start + this->object_count * this->object_size;
      _ASSERT(this->buffer_end <= this->region_bytes);

      // Compute layout policy
      if (this->object_size == 0) this->layoutPolicy = UncachedLargeObjectPolicy;
      else if (this->object_count > 1) this->layoutPolicy = SlabbedObjectPolicy;
      else this->layoutPolicy = LargeObjectPolicy;

      //_ASSERT(this->slab_count <= cst::ObjectSlabPerRegionMax);
   }
   std::string getLayoutPolicyName() {
      switch (this->layoutPolicy) {
      case UncachedLargeObjectPolicy: return "ins::UncachedLargeObjectPolicy";
      case SlabbedObjectPolicy: return "ins::SlabbedObjectPolicy";
      case LargeObjectPolicy: return "ins::LargeObjectPolicy";
      default: throw;
      }
   }
   uint32_t getDivider() {
      auto div = ((uint64_t(1) << 16) + object_size - 1) / object_size;
      auto div2 = ((uint64_t(1) << 16) / object_size) - 1;
      for (int i = 0; i < cst::PageSize; i++) {
         auto r = applyBlockDivider(div, i);
         auto r2 = applyBlockDivider(div2, i);
         auto e = i / this->object_size;
         if (r2 != e) {
            throw;
         }
      }
      return div;
   }
   void print() {
      printf("[%s] size=%d;objects=%d;slabs=%d;regionsz=%d;lost=%d\n",
         this->getLayoutPolicyName().c_str(), object_size, object_count,
         slab_count, size_t(1) << region_sizeL2, lost_bytes
      );
   }
   static int __cdecl compare_object_size(tObjectClass* x, tObjectClass* y) {
      return x->object_size - y->object_size;
   }
   static int __cdecl compare_layout_order(tObjectClass*& x, tObjectClass*& y) {
      if (auto c = int(x->layoutPolicy) - int(y->layoutPolicy)) return c;
      return x->object_size - y->object_size;
   }
   static size_t keepSizeBit(size_t sz, int n) {
      int k = msb_64(sz) + 1;
      size_t mask = (k > n) ? (size_t(-1) << (k - n)) : -1;
      return sz & mask;
   }
   bool isValid() {
      if (this->object_count == 0) return false;
      if (this->region_sizeL2 < cst::PageSizeL2) {
         if (this->object_count < 24) return false;
      }
      return true;
   }
};

std::vector<tObjectClass> computeObjectPotentialClasses(double gap_growth, size_t* page_templates, int page_templates_count, size_t min_size = 16) {
   std::vector<tObjectClass> classes;
   classes.push_back(tObjectClass(UncachedLargeObjectPolicy, 0));
   for (size_t page_sizeL2 = page_templates[0]; page_sizeL2 <= page_templates[page_templates_count - 1]; page_sizeL2++) {
      tObjectClass obj((size_t(1) << page_sizeL2) - cst::ObjectRegionHeadSize - sizeof(sObjectSlab), page_sizeL2);
      if (obj.isValid()) {
         _ASSERT(obj.lost_bytes == 0 && obj.object_count == 1);
         obj.region_sizeL2 = 0;
         classes.push_back(obj);
      }
   }
   size_t size_mask = ~size_t(7);
   classes[0].object_size = min_size - 8;
   for (size_t k = 0; k < page_templates_count; k++) {
      size_t page_sizeL2 = page_templates[k];
      tObjectClass prev = classes[0];
      size_t prev_class_count = classes.size();
      for (size_t i = 1; i < prev_class_count; i++) {
         tObjectClass prev_cls = classes[i - 1];
         tObjectClass cur_cls = classes[i];
         size_t max_object_size = cur_cls.object_size - std::max<uint64_t>(uint64_t(cur_cls.object_size * gap_growth) & size_mask, 8);
         size_t min_object_size = prev_cls.object_size + std::max<uint64_t>(uint64_t(prev_cls.object_size * gap_growth) & size_mask, 8);
         prev = prev_cls;
         for (size_t object_size = min_object_size; object_size <= max_object_size; object_size += 8) {
            tObjectClass next(object_size, page_sizeL2);
            if (next.isValid()) {
               if (prev.object_count != next.object_count) {
                  if (prev.region_sizeL2 == page_sizeL2) classes.push_back(prev);
               }
               prev = next;
            }
         }
         prev = cur_cls;
      }
      std::qsort(&classes[0], classes.size(), sizeof(tObjectClass), (_CoreCrtNonSecureSearchSortCompareFunction)tObjectClass::compare_object_size);
   }
   classes[0].object_size = 0;
   return classes;
}

std::vector<tObjectClass> filterObjectClasses(std::vector<tObjectClass> classes, size_t min_size, double min_growth, double max_growth) {
   std::vector<tObjectClass> selected_classes;
   selected_classes.push_back(classes[0]);
   for (size_t i = 1; i < classes.size(); i++) {
      auto& cur_cls = classes[i];
      if (cur_cls.object_size < min_size) {
         selected_classes.push_back(cur_cls);
      }
      else {
         auto last_object_size = selected_classes.back().object_size;
         auto& prev_cls = classes[i - 1];
         double prev_growth = double(prev_cls.object_size - last_object_size) / double(prev_cls.object_size);
         double cur_growth = double(cur_cls.object_size - last_object_size) / double(cur_cls.object_size);
         if (cur_growth >= min_growth) {
            if (cur_growth <= max_growth || (cur_growth <= max_growth && prev_growth <= min_growth) || prev_growth <= min_growth) {
               last_object_size = cur_cls.object_size;
               cur_cls.selected = true;
               selected_classes.push_back(cur_cls);
            }
            else {
               last_object_size = prev_cls.object_size;
               prev_cls.selected = true;
               selected_classes.push_back(prev_cls);
            }
         }
      }
   }
   return selected_classes;
}

void generate_objects_layout_config(std::string path) {
   const double min_growth = 0.10;
   const double max_growth = 0.12;
   size_t pages[] = { 13,14,15,16,17,18,19,20 };
   auto classes = computeObjectPotentialClasses(0.02, pages, sizeof(pages) / sizeof(pages[0]));
   classes = filterObjectClasses(classes, 128, min_growth, max_growth);
   classes.erase(classes.begin(), classes.begin() + 1);
   classes.push_back(tObjectClass(UncachedLargeObjectPolicy, cst::ArenaSize));

   size_t cache_size = 0;
   size_t used_count = 0;
   size_t prev_size = 0;
   for (auto& cls : classes) {
      float growth = 100 * float(cls.object_size - prev_size) / float(cls.object_size);
      printf("^%.3g%%\t", growth);
      if (cls.region_sizeL2) used_count++;
      cls.print();
      prev_size = cls.object_size;
      if (cls.object_count > 1 && cls.region_sizeL2 <= cst::PageSizeL2) {
         cache_size += cls.region_bytes - cls.object_size;
      }
   }
   printf("classes=%d/%d\n", used_count, classes.size());
   printf("cache_size=%d/%d\n", cache_size);

   std::vector<tObjectClass*> layouts;
   size_t SlabbedObjectPolicy_count = 0;
   size_t LargeObjectPolicy_count = 0;
   size_t NoObjectPolicy_count = 0;
   for (size_t i = 0; i < classes.size(); i++) {
      layouts.push_back(&classes[i]);
   }
   std::qsort(&layouts[0], layouts.size(), sizeof(tObjectClass*), (_CoreCrtNonSecureSearchSortCompareFunction)tObjectClass::compare_layout_order);
   for (size_t i = 0; i < layouts.size(); i++) {
      layouts[i]->layoutID = i;
      switch (layouts[i]->layoutPolicy) {
      case SlabbedObjectPolicy:
         SlabbedObjectPolicy_count = i;
         break;
      case LargeObjectPolicy:
         LargeObjectPolicy_count = i;
         break;
      case UncachedLargeObjectPolicy:
         NoObjectPolicy_count = i;
         break;
      default: throw;
      }
   }

   const size_t LayoutRangeSizeCount = 512;
   const size_t SmallSizeLimit = LayoutRangeSizeCount << 3;
   const size_t MediumSizeLimit = SmallSizeLimit << 4;
   const size_t LargeSizeLimit = MediumSizeLimit << 4;

   struct tLayoutRangeBin {
      uint8_t layoutMin = 0, layoutMax = 0;
   };

   uint8_t small_object_layouts[LayoutRangeSizeCount + 1] = { 0 };
   tLayoutRangeBin medium_object_layouts[LayoutRangeSizeCount];
   tLayoutRangeBin large_object_layouts[LayoutRangeSizeCount];

   uint8_t clsIndex = 1;
   while (clsIndex < classes.size()) {
      auto& cls = classes[clsIndex];
      const size_t size_step = SmallSizeLimit / LayoutRangeSizeCount;
      size_t end_index = std::min(cls.object_size / size_step, LayoutRangeSizeCount);
      for (size_t i = end_index; i >= 0 && small_object_layouts[i] == 0; i--) {
         small_object_layouts[i] = cls.layoutID;
      }
      if (cls.object_size >= SmallSizeLimit) {
         _ASSERT(end_index == LayoutRangeSizeCount);
         break;
      }
      clsIndex++;
   }
   while (clsIndex < classes.size()) {
      auto& cls = classes[clsIndex];
      const size_t size_step = MediumSizeLimit / LayoutRangeSizeCount;
      size_t prev_index = classes[clsIndex - 1].object_size / size_step;
      medium_object_layouts[prev_index].layoutMax = cls.layoutID;
      for (size_t i = prev_index + 1; i < LayoutRangeSizeCount && (i * size_step) <= cls.object_size; i++) {
         auto& bin = medium_object_layouts[i];
         bin.layoutMin = cls.layoutID;
         bin.layoutMax = cls.layoutID;
      }
      if (cls.object_size >= MediumSizeLimit) {
         break;
      }
      clsIndex++;
   }
   while (clsIndex < classes.size()) {
      auto& cls = classes[clsIndex];
      const size_t size_step = LargeSizeLimit / LayoutRangeSizeCount;
      size_t prev_index = classes[clsIndex - 1].object_size / size_step;
      large_object_layouts[prev_index].layoutMax = cls.layoutID;
      for (size_t i = prev_index + 1; i < LayoutRangeSizeCount && (i * size_step) <= cls.object_size; i++) {
         auto& bin = large_object_layouts[i];
         bin.layoutMin = cls.layoutID;
         bin.layoutMax = cls.layoutID;
      }
      if (cls.object_size >= LargeSizeLimit) {
         break;
      }
      clsIndex++;
   }
   if (clsIndex == classes.size()) {
      auto& cls = classes[classes.size() - 1];
      _ASSERT(cls.layoutPolicy == UncachedLargeObjectPolicy);
      const size_t size_step = LargeSizeLimit / LayoutRangeSizeCount;
      size_t prev_index = classes[clsIndex - 1].object_size / size_step;
      large_object_layouts[prev_index].layoutMax = cls.layoutID;
      for (size_t i = prev_index + 1; i < LayoutRangeSizeCount; i++) {
         auto& bin = large_object_layouts[i];
         bin.layoutMin = cls.layoutID;
         bin.layoutMax = cls.layoutID;
      }
   }

   for (int i = 0; i < LayoutRangeSizeCount; i++) {
      if (medium_object_layouts[i].layoutMin > 0) {
         auto sizeMin = layouts[medium_object_layouts[i].layoutMin]->object_size;
         auto sizeMax = layouts[medium_object_layouts[i].layoutMax]->object_size;
         _ASSERT(sizeMax >= sizeMin);
      }
      if (large_object_layouts[i].layoutMin > 0) {
         auto sizeMin = layouts[large_object_layouts[i].layoutMin]->object_size;
         auto sizeMax = layouts[large_object_layouts[i].layoutMax]->object_size;
         _ASSERT(sizeMax >= sizeMin);
      }
   }

   auto getLayoutForSize = [&](size_t size) -> uint8_t {
      if (size < SmallSizeLimit) {
         const size_t size_step = SmallSizeLimit / LayoutRangeSizeCount;
         auto index = (size + 7) / size_step;
         return small_object_layouts[index];
      }
      else if (size < MediumSizeLimit) {
         const size_t size_step = MediumSizeLimit / LayoutRangeSizeCount;
         auto& bin = medium_object_layouts[size / size_step];
         if (size <= layouts[bin.layoutMin]->object_size) return bin.layoutMin;
         else return bin.layoutMax;
      }
      else if (size < LargeSizeLimit) {
         const size_t size_step = LargeSizeLimit / LayoutRangeSizeCount;
         auto& bin = large_object_layouts[size / size_step];
         if (size <= layouts[bin.layoutMin]->object_size) return bin.layoutMin;
         else return bin.layoutMax;
      }
      else {
         return ObjectLayoutCount - 1;
      }
   };
   for (size_t sz = 0; sz < LargeSizeLimit * 2; sz++) {
      auto layout = getLayoutForSize(sz);
      if (layout == 0) {
         printf("! no match at: %lld\n", sz);
         _ASSERT(0);
      }
      else if (layout > 1) {
         auto cls = layouts[layout];
         if (cls[0].object_size < sz) {
            printf("! too small at: %lld\n", sz);
            _ASSERT(0);
         }
         else if (cls[-1].object_size >= sz) {
            printf("! too large at: %lld\n", sz);
            _ASSERT(0);
         }
      }
   }

   {
      std::ofstream out(path + "/include/ins/memory/objects-config.h");
      out << "#include <ins/memory/objects.h>\n\n";
      out << "namespace ins {\n";
      out << "\n";
      out << "const size_t SlabbedObjectLayoutMin  = " << 0 << ";\n";
      out << "const size_t SlabbedObjectLayoutMax = " << (SlabbedObjectPolicy_count - 1) << ";\n";
      out << "const size_t LargeObjectLayoutMin  = " << SlabbedObjectPolicy_count << ";\n";
      out << "const size_t LargeObjectLayoutMax = " << (LargeObjectPolicy_count - 1) << ";\n";
      out << "const size_t UncachedLargeObjectLayoutMin = " << LargeObjectPolicy_count << ";\n";
      out << "const size_t UncachedLargeObjectLayoutMax = " << LargeObjectPolicy_count << ";\n";
      out << "\n";
      out << "const size_t ObjectLayoutCount = " << classes.size() << ";\n";
      out << "\n";
      out << "const size_t LayoutRangeSizeCount = " << LayoutRangeSizeCount << ";\n";
      out << "const size_t SmallSizeLimit = LayoutRangeSizeCount << 3;\n";
      out << "const size_t MediumSizeLimit = SmallSizeLimit << 4;\n";
      out << "const size_t LargeSizeLimit = MediumSizeLimit << 4;\n";
      out << "\n";
      out << "struct tLayoutRangeBin {uint8_t layoutMin = 0, layoutMax = 0;};\n";
      out << "extern const uint8_t small_object_layouts[LayoutRangeSizeCount + 1];\n";
      out << "extern const tLayoutRangeBin medium_object_layouts[LayoutRangeSizeCount];\n";
      out << "extern const tLayoutRangeBin large_object_layouts[LayoutRangeSizeCount];\n";
      out << "}\n";
      out.close();
   }
   {
      char tmp[128];

      std::ofstream out(path + "/src/memory/objects-config.cpp");
      out << "#include <ins/memory/objects.h>\n\n";

      out << "const ins::tObjectLayoutBase ins::ObjectLayoutBase[ins::ObjectLayoutCount] = {\n";
      for (int i = 0; i < layouts.size(); i++) {
         auto& cls = *layouts[i];
         out << "{";
         out << "/*object_base*/" << cls.buffer_start << ", ";
         out << "/*object_divider*/" << cls.object_divider << ", ";
         out << "/*object_size*/" << cls.object_size << ", ";
         out << "/*region_mask*/ 0x" << itoa(cls.region_mask, tmp, 16) << ", ";
         out << "},\n";
      }
      out << "};\n\n";

      out << "const ins::tObjectLayoutInfos ins::ObjectLayoutInfos[ins::ObjectLayoutCount] = {\n";
      for (int i = 0; i < layouts.size(); i++) {
         auto& cls = *layouts[i];
         out << "{";
         out << "/*object_base*/" << cls.buffer_start << ", ";
         out << "/*object_divider*/" << cls.object_divider << ", ";
         out << "/*object_size*/" << cls.object_size << ", ";
         out << "/*object_count*/" << cls.object_count << ", ";
         out << "/*slab_count*/" << cls.slab_count << ", ";
         out << "/*region_sizeL2*/" << cls.region_sizeL2 << ", ";
         out << "/*region_mask*/ 0x" << itoa(cls.region_mask, tmp, 16) << ", ";
         out << "/*policy*/" << cls.getLayoutPolicyName() << ", ";
         out << "},\n";
      }
      out << "};\n\n";

      out << "const uint8_t ins::small_object_layouts[ins::LayoutRangeSizeCount+1] = {";
      for (size_t i = 0; i <= LayoutRangeSizeCount; i++) {
         if ((i % 32) == 0) out << "\n";
         out << itoa(small_object_layouts[i], tmp, 10) << ", ";
      }
      out << "\n};\n\n";

      out << "const ins::tLayoutRangeBin ins::medium_object_layouts[ins::LayoutRangeSizeCount] = {";
      for (size_t i = 0; i < LayoutRangeSizeCount; i++) {
         if ((i % 16) == 0) out << "\n";
         out << "{" << itoa(medium_object_layouts[i].layoutMin, tmp, 10);
         out << "," << itoa(medium_object_layouts[i].layoutMax, tmp, 10) << "}, ";
      }
      out << "\n};\n\n";

      out << "const ins::tLayoutRangeBin ins::large_object_layouts[ins::LayoutRangeSizeCount] = {";
      for (size_t i = 0; i < LayoutRangeSizeCount; i++) {
         if ((i % 16) == 0) out << "\n";
         out << "{" << itoa(large_object_layouts[i].layoutMin, tmp, 10);
         out << "," << itoa(large_object_layouts[i].layoutMax, tmp, 10) << "}, ";
      }
      out << "\n};\n\n";

      out.close();
   }
}

int main() {
   generate_objects_layout_config("C:/git/project/insmalloc/lib/ins.memory.space");
}
