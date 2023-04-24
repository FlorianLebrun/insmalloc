#include <ins/binary/alignment.h>
#include <ins/memory/space.h>
#include <ins/memory/structs.h>
#include <stdio.h>
#include <vector>
#include <fstream>
#include <iostream>

using namespace ins;

struct tRegionClass {
   int region_templateID = -1;
   size_t region_sizeL2 = 0;
   size_t region_sizing = 0;

   tRegionClass* sizings = 0;

   size_t region_size = 0;
   size_t region_granularityL2 = 0;
   size_t region_pageSizeL2 = 0;
   size_t region_pageSize = 0;
   size_t region_pages = 0;
   size_t region_committedPages = 0;
   size_t region_committedBytes = 0;
   size_t region_retention = 0;
   uint32_t region_mask = -1;

   tRegionClass(size_t region_sizeL2, size_t region_sizing, size_t pageSizeL2, size_t commitedSize, size_t region_retention) {
      this->region_sizeL2 = region_sizeL2;
      this->region_sizing = region_sizing;

      this->region_size = size_t(1) << region_sizeL2;
      this->region_mask = ((size_t(1) << region_sizeL2) - 1);
      this->region_retention = region_retention * 16;

      this->region_pageSizeL2 = pageSizeL2;
      this->region_pageSize = size_t(1) << pageSizeL2;
      this->region_pages = size_t(1) << (region_sizeL2 - pageSizeL2);
      this->region_granularityL2 = (pageSizeL2 > region_sizeL2) ? region_sizeL2 : pageSizeL2;

      this->region_committedPages = commitedSize / this->region_pageSize;
      if (commitedSize > 0 && this->region_committedPages == 0) this->region_committedPages = 1;
      this->region_committedBytes = this->region_committedPages * this->region_pageSize;
      _ASSERT(this->region_committedBytes >= commitedSize);
   }
   tRegionClass* push(tRegionClass* cls) {
      this->sizings = cls;
      return cls;
   }
   void print() {
      printf("region size=%lld\tretention=%lld\n",
         this->region_committedBytes, this->region_retention
      );
   }
};

struct tRegionManifold {
   std::vector<tRegionClass*> regions;
   tRegionClass* getRegionClass(size_t region_sizeL2) {
      for (auto r : this->regions) {
         if (r->region_sizeL2 == region_sizeL2) {
            return r;
         }
      }
      throw;
   }
   tRegionClass* addRegion(tRegionClass* sizing) {
      this->regions.push_back(sizing);
      return sizing;
   }
   void computeRegionClasses(size_t minRegionL2, size_t cachedRegionSizeMaxL2) {
      auto page_szL2 = 16;
      auto page_sz = size_t(1) << page_szL2;
      for (int szL2 = 0; szL2 < minRegionL2; szL2++) {
         regions.push_back(new tRegionClass(szL2, 0, 0, 0, 0));
      }
      for (int szL2 = minRegionL2; szL2 < cachedRegionSizeMaxL2; szL2++) {
         auto sz = size_t(1) << szL2;
         auto delta_4 = size_t(1) << (szL2 - 2);
         auto delta_8 = size_t(1) << (szL2 - 3);
         auto delta_16 = size_t(1) << (szL2 - 4);
         if (delta_16 >= page_sz) {
            this->addRegion(new tRegionClass(szL2, 0, page_szL2, sz - 0 * delta_16, 16))
               ->push(new tRegionClass(szL2, 1, page_szL2, sz - 3 * delta_16, 4))
               ->push(new tRegionClass(szL2, 2, page_szL2, sz - 5 * delta_16, 4))
               ->push(new tRegionClass(szL2, 3, page_szL2, sz - 7 * delta_16, 4));
         }
         else if (delta_8 >= page_sz) {
            this->addRegion(new tRegionClass(szL2, 0, page_szL2, sz - 0 * delta_8, 16))
               ->push(new tRegionClass(szL2, 1, page_szL2, sz - 3 * delta_8, 2));
         }
         else if (sz >= page_sz) {
            this->addRegion(new tRegionClass(szL2, 0, page_szL2, sz, 16));
         }
         else {
            this->addRegion(new tRegionClass(szL2, 0, page_szL2, sz, 16));
         }
      }
      for (int szL2 = cachedRegionSizeMaxL2; szL2 <= cst::ArenaSizeL2; szL2++) {
         auto sz = size_t(1) << szL2;
         this->addRegion(new tRegionClass(szL2, 0, page_szL2, sz, 0));
      }
   }
   void print() {
      size_t prev_sz = 0;
      for (auto r : this->regions) {
         auto sz = r->region_committedBytes;
         float growth = 100 * float(sz - prev_sz) / float(sz);
         printf("^%.3g%%\t", growth);
         r->print();
         prev_sz = sz;
      }
   }
};

tRegionManifold regionManifold;

struct tObjectClass {
   uint32_t layoutID = 0;
   ObjectLayoutPolicy layoutPolicy = LargeObjectPolicy;

   tRegionClass* region = 0;
   size_t buffer_start = 0;
   size_t buffer_end = 0;
   size_t object_size = 0;
   size_t object_count = 0;
   size_t object_divider = 0;
   size_t object_multiplier = 0;
   size_t lost_bytes = 0;
   bool selected = false;

   struct {
      uint32_t list_length = 0;
      uint32_t heap_count = 0;
      uint32_t context_count = 0;
   } retention;

   tObjectClass(ObjectLayoutPolicy layoutPolicy, size_t object_size) {
      this->layoutPolicy = layoutPolicy;
      this->object_size = object_size;
      this->region = regionManifold.getRegionClass(cst::ArenaSizeL2);
      this->buffer_start = cst::ObjectRegionHeadSize;
      this->buffer_end = this->region->region_size;
   }
   tObjectClass(size_t object_size, size_t page_sizeL2) : object_size(object_size) {
      this->region = regionManifold.getRegionClass(page_sizeL2);

      // Approximate slab count that contains all objects containable by the available bytes
      size_t usable_bytes = this->region->region_size - cst::ObjectRegionHeadSize;
      this->object_count = usable_bytes / object_size;
      if (this->object_count > cst::ObjectPerSlab) {
         this->object_count = 0;
      }

      // Compute layout policy
      if (this->object_count > 1) {
         this->layoutPolicy = SmallObjectPolicy;
         this->buffer_start = cst::ObjectRegionHeadSize;
         this->buffer_end = buffer_start + this->object_count * this->object_size;
         this->object_divider = ins::getBlockDivider(object_size);
         this->object_multiplier = this->object_size;
         _ASSERT(this->buffer_end <= this->region->region_size);
         size_t private_retention_size = size_t(1) << 16;
         this->retention.list_length = std::max<int>(1, std::min<int>(1024, (private_retention_size / 2) / this->object_size));
         this->retention.context_count = this->retention.list_length * 2;
         this->retention.heap_count = this->retention.list_length * 8;
      }
      else {
         if (this->object_size == 0) this->layoutPolicy = LargeObjectPolicy;
         else this->layoutPolicy = MediumObjectPolicy;
         this->buffer_start = cst::ObjectRegionHeadSize;
         this->buffer_end = this->region->region_size;
         this->object_size = this->buffer_end - this->buffer_start;
         this->object_divider = 0;
         this->object_multiplier = 0;
         this->retention.list_length = 1;
         this->retention.context_count = 0;
         this->retention.heap_count = 0;
      }
   }
   std::string getLayoutPolicyName() {
      switch (this->layoutPolicy) {
      case LargeObjectPolicy: return "ins::LargeObjectPolicy";
      case SmallObjectPolicy: return "ins::SmallObjectPolicy";
      case MediumObjectPolicy: return "ins::MediumObjectPolicy";
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
      printf("[%s] size=%zu\tobjects=%zu\tregionszL2=%zu\tlost=%zu  \tretention(list=%d heap=%d context=%d)\n",
         this->getLayoutPolicyName().c_str(), object_size, object_count,
         this->region->region_sizeL2, lost_bytes,
         this->retention.list_length, this->retention.heap_count, this->retention.context_count
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
      if (this->region->region_sizeL2 < cst::PageSizeL2) {
         if (this->object_count < 24) return false;
      }
      return true;
   }
};

std::vector<tObjectClass> computeObjectPotentialClasses(double gap_growth, size_t* page_templates, int page_templates_count, size_t min_size = 16) {
   std::vector<tObjectClass> classes;

   // Initiate with large object policy classes
   classes.push_back(tObjectClass(LargeObjectPolicy, 0));
   for (size_t page_sizeL2 = page_templates[0]; page_sizeL2 <= page_templates[page_templates_count - 1]; page_sizeL2++) {
      tObjectClass obj((size_t(1) << page_sizeL2) - cst::ObjectRegionHeadSize, page_sizeL2);
      if (obj.isValid()) {
         _ASSERT(obj.lost_bytes == 0 && obj.object_count == 1);
         classes.push_back(obj);
      }
   }

   // Complete with slabbed object policy classes
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
                  if (prev.region->region_sizeL2 == page_sizeL2) classes.push_back(prev);
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

void generate_objects_config(std::string src_path) {
   const double min_growth = 0.10;
   const double max_growth = 0.12;
   size_t pages[] = { 10,11,12,13,14,15,16,17,18,19,20 };
   regionManifold.computeRegionClasses(10, 20);

   auto classes = computeObjectPotentialClasses(0.02, pages, sizeof(pages) / sizeof(pages[0]));
   classes = filterObjectClasses(classes, 128, min_growth, max_growth);
   classes.erase(classes.begin(), classes.begin() + 1);
   classes.push_back(tObjectClass(LargeObjectPolicy, cst::ArenaSize));

   size_t heap_cache_size = 0;
   size_t context_cache_size = 0;
   size_t private_cache_size = 0;
   size_t used_count = 0;
   size_t prev_size = 0;
   for (auto& cls : classes) {
      float growth = 100 * float(cls.object_size - prev_size) / float(cls.object_size);
      printf("^%.3g%%\t", growth);
      if (cls.region->region_sizeL2) used_count++;
      cls.print();
      prev_size = cls.object_size;
      if (cls.object_count > 1 && cls.region->region_sizeL2 <= cst::PageSizeL2) {
         heap_cache_size += cls.object_size * cls.retention.heap_count;
         context_cache_size += cls.object_size * cls.retention.context_count;
         private_cache_size += cls.region->region_size - cls.object_size;
      }
   }
   printf("classes=%zu/%zu\n", used_count, classes.size());
   printf("heap_cache_size=%zu\n", heap_cache_size);
   printf("context_cache_size=%zu\n", context_cache_size);
   printf("private_cache_size=%zu\n", private_cache_size);

   // Collect and sort objects layouting pattern
   //----------------------------------------------
   std::vector<tObjectClass*> layouts;
   for (size_t i = 0; i < classes.size(); i++) {
      layouts.push_back(&classes[i]);
   }
   std::qsort(&layouts[0], layouts.size(), sizeof(tObjectClass*), (_CoreCrtNonSecureSearchSortCompareFunction)tObjectClass::compare_layout_order);
   for (size_t i = 0; i < layouts.size(); i++) {
      layouts[i]->layoutID = i;
   }

   // Configure objects size/id matching table
   //----------------------------------------------
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
      _ASSERT(cls.layoutPolicy == LargeObjectPolicy);
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

   // Test objects size/id matching table
   //----------------------------------------------
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
         return layouts.size() - 1;
      }
   };
   for (size_t sz = 0; sz < LargeSizeLimit * 2; sz++) {
      auto layoutID = getLayoutForSize(sz);
      if (layoutID == 0) {
         printf("! no match at: %lld\n", sz);
         _ASSERT(0);
      }
      else if (layoutID > 1) {
         auto cls = layouts[layoutID];
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

   // Configure objects region template
   //----------------------------------------------
   std::vector<tRegionClass*> rtemplates;
   for (size_t i = 0; i < layouts.size(); i++) {
      auto r = layouts[i]->region;
      if (r->region_templateID == -1) {
         r->region_templateID = rtemplates.size();
         rtemplates.push_back(r);
      }
   }

   // Generate config
   //----------------------------------------------
   {
      std::ofstream out(path + "/ins.memory.heap/include/ins/memory/config.h");
      out << "#pragma once\n";
      out << "namespace ins::mem::cst {\n";
      out << "\n";
      out << "   const size_t ObjectLayoutMin  = " << 0 << ";\n";
      out << "   const size_t ObjectLayoutMax = " << (classes.size() - 1) << ";\n";
      out << "   const size_t ObjectLayoutCount = " << classes.size() << ";\n";
      out << "\n";
      out << "   const size_t ObjectRegionTemplateCount = " << rtemplates.size() << ";\n";
      out << "\n";
      out << "   const size_t LayoutRangeSizeCount = " << LayoutRangeSizeCount << ";\n";
      out << "   const size_t SmallSizeLimit = LayoutRangeSizeCount << 3;\n";
      out << "   const size_t MediumSizeLimit = SmallSizeLimit << 4;\n";
      out << "   const size_t LargeSizeLimit = MediumSizeLimit << 4;\n";
      out << "\n";
      out << "   struct tLayoutRangeBin {uint8_t layoutMin = 0, layoutMax = 0;};\n";
      out << "   extern const uint8_t small_object_layouts[LayoutRangeSizeCount + 1];\n";
      out << "   extern const tLayoutRangeBin medium_object_layouts[LayoutRangeSizeCount];\n";
      out << "   extern const tLayoutRangeBin large_object_layouts[LayoutRangeSizeCount];\n";
      out << "}\n";
      out.close();
   }
   {
      std::ofstream out(path + "/ins.memory.heap/src/memory/config.cpp");
      out << "#include <ins/memory/contexts.h>\n";
      out << "#include <ins/memory/regions.h>\n";
      out << "\n";
      char tmp[128];

      out << "const ins::tObjectLayoutBase ins::cst::ObjectLayoutBase[ins::cst::ObjectLayoutCount] = {\n";
      for (int i = 0; i < layouts.size(); i++) {
         auto& cls = *layouts[i];
         out << "{";
         out << "/*object_divider*/" << cls.object_divider << ", ";
         out << "/*object_multiplier*/" << cls.object_multiplier << ", ";
         out << "},\n";
      }
      out << "};\n\n";

      out << "const uint64_t ins::cst::ObjectLayoutMask[ins::cst::ObjectLayoutCount] = {\n";
      for (int i = 0; i < layouts.size(); i++) {
         auto& cls = *layouts[i];         
         if (cls.object_count > 64) throw;
         sprintf(tmp, "0x%llx", lmask_64(cls.object_count));
         out << tmp << ", ";
      }
      out << "};\n\n";

      out << "const ins::tObjectRegionTemplate ins::cst::ObjectRegionTemplate[ins::cst::ObjectRegionTemplateCount] = {\n";
      for (int i = 0; i < rtemplates.size(); i++) {
         auto& rtpl = *rtemplates[i];
         out << "{";
         out << "/*region_sizeL2*/" << rtpl.region_sizeL2 << ", ";
         out << "/*region_sizing*/" << rtpl.region_sizing << ", ";
         out << "},\n";
      }
      out << "};\n\n";

      out << "const ins::tObjectLayoutInfos ins::cst::ObjectLayoutInfos[ins::cst::ObjectLayoutCount] = {\n";
      for (int i = 0; i < layouts.size(); i++) {
         auto& cls = *layouts[i];
         out << "{";
         out << "/*region_objects*/" << cls.object_count << ", ";
         out << "/*region_templateID*/" << cls.region->region_templateID << ", ";
         out << "/*region_sizeL2*/" << cls.region->region_sizeL2 << ", ";
         out << "/*region_sizing*/" << cls.region->region_sizing << ", ";
         out << "/*policy*/" << cls.getLayoutPolicyName() << ", ";
         out << "/*retention*/{" << cls.retention.list_length << "," << cls.retention.heap_count << "," << cls.retention.context_count << "}, ";
         out << "},\n";
      }
      out << "};\n\n";

      out << "const uint8_t ins::cst::small_object_layouts[ins::cst::LayoutRangeSizeCount+1] = {";
      for (size_t i = 0; i <= LayoutRangeSizeCount; i++) {
         if ((i % 32) == 0) out << "\n";
         out << itoa(small_object_layouts[i], tmp, 10) << ", ";
      }
      out << "\n};\n\n";

      out << "const ins::cst::tLayoutRangeBin ins::cst::medium_object_layouts[ins::cst::LayoutRangeSizeCount] = {";
      for (size_t i = 0; i < LayoutRangeSizeCount; i++) {
         if ((i % 16) == 0) out << "\n";
         out << "{" << itoa(medium_object_layouts[i].layoutMin, tmp, 10);
         out << "," << itoa(medium_object_layouts[i].layoutMax, tmp, 10) << "}, ";
      }
      out << "\n};\n\n";

      out << "const ins::cst::tLayoutRangeBin ins::cst::large_object_layouts[ins::cst::LayoutRangeSizeCount] = {";
      for (size_t i = 0; i < LayoutRangeSizeCount; i++) {
         if ((i % 16) == 0) out << "\n";
         out << "{" << itoa(large_object_layouts[i].layoutMin, tmp, 10);
         out << "," << itoa(large_object_layouts[i].layoutMax, tmp, 10) << "}, ";
      }
      out << "\n};\n\n";
   }
   {
      std::ofstream out(src_path + "/ins.memory.space/src/memory/config.cpp");
      out << "#include <ins/memory/contexts.h>\n";
      out << "#include <ins/memory/regions.h>\n";
      out << "\n";
      char tmp[128];

      out << "const ins::tRegionSizingInfos ins::cst::RegionSizingInfos[cst::RegionSizingCount] = {\n";
      for (int szL2 = 0; szL2 < regionManifold.regions.size(); szL2++) {
         auto& cls = *regionManifold.regions[szL2];
         out << "/*sizeL2=" << cls.region_sizeL2 << "*/ ";
         out << "{";
         out << "/*granularityL2*/" << cls.region_granularityL2 << ", ";
         out << "/*pageSizeL2*/" << cls.region_pageSizeL2 << ", ";
         out << "/*layouts{retention,committedPages,committeSize}*/{";
         for (auto sizing = &cls; sizing; sizing = sizing->sizings) {
            out << "{";
            out << sizing->region_retention << ", ";
            out << sizing->region_committedPages << ", ";
            out << sizing->region_committedBytes << ", ";
            out << "}, ";
         }
         out << "}, ";
         out << "},\n";
      }
      out << "};\n\n";

      out << "const uint32_t ins::cst::RegionMasks[cst::RegionSizingCount] = {";
      for (int szL2 = 0; szL2 < regionManifold.regions.size(); szL2++) {
         auto& cls = *regionManifold.regions[szL2];
         if ((szL2 % 12) == 0) out << "\n";
         out << "0x" << itoa(cls.region_mask, tmp, 16) << ", ";
      }
      out << "\n};\n\n";

      out.close();
   }
}

int main() {
   generate_objects_config("C:/git/project/insmalloc/lib");
}
