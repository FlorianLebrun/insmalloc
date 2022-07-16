#include <ins/binary/alignment.h>
#include <ins/memory/configuration.h>
#include "./blocks.h"

using namespace ins;

uint64_t get_usables_bits(size_t count) {
   if (count == 64) return -1;
   else return (uint64_t(1) << count) - 1;
}

void ins::printAllBlocks() {
   for (int i = 0; i < cBlockClassCount; i++) {
      cBlockClassTable[i]->print();
   }
}

ElementClass::ElementClass(uint8_t id) : id(id) {

}

SlabClass::SlabClass(uint8_t id) : ElementClass(id) {

}

BlockClass::BlockClass(uint8_t id) : ElementClass(id) {

}

/*****************************************************
*
*  PnS1 Class
*
******************************************************/

SlabPnS1Class::SlabPnS1Class(uint8_t id, uint8_t binID, uint8_t layoutID, uint8_t packing, uint8_t shift)
   : SlabClass(id), sizing(packing, shift)
{
   size_t slab_size = size_t(packing) << shift;
   this->binID = binID;
   this->layoutID = layoutID;
   this->slab_per_batch = ins::cPageSize / slab_size;
   if (this->slab_last_size = ins::cPageSize % slab_size) this->slab_per_batch++;
   else this->slab_last_size = slab_size;
}

tpSlabDescriptor SlabPnS1Class::allocate(MemoryContext* context) {
   auto& bin = context->space->slabs_bins[this->binID];
   if (auto slab = bin.pop(context->space, context->id)) {
      return slab;
   }

   address_t area = context->space->acquirePageSpan(1, 0);
   auto batch = tpSlabBatchDescriptor(context->allocateSystemMemory(this->slab_per_batch + size_t(1)));
   memset(batch, 0, (this->slab_per_batch + size_t(1)) * 64);
   batch->usables = get_usables_bits(this->slab_per_batch);
   batch->uses = 0;
   batch->length = this->slab_per_batch;
   batch->page_index = area.pageIndex;
   batch->gc_marks = 0;
   batch->gc_analyzis = 0;
   batch->class_id = this->id;
   batch->next = bin.batches;
   bin.batches = batch;

   auto& entry = context->space->regions[area.regionID]->pages_table[area.pageID];
   entry.reference = uint64_t(&batch[1]);
   entry.layoutID = this->layoutID;

   return bin.pop(context->space, context->id);
}

void SlabPnS1Class::release(tpSlabDescriptor slab, MemoryContext* context) {
   auto batch = tpSlabBatchDescriptor(&slab[-slab->slab_position]);
   uint64_t slab_bit = uint64_t(1) << (slab->slab_position - 1);
   _ASSERT(batch->uses & slab_bit);
   batch->uses ^= slab_bit;
   if (batch->uses == 0) {
      printf("lost batch\n");
   }
}

BlockPnS1Class::BlockPnS1Class(uint8_t id, uint8_t binID, uint8_t packing, uint8_t shift, SlabPnS1Class* slab_class)
   : BlockClass(id), sizing(packing, shift), slab_class(slab_class) {
   this->binID = binID;
   this->block_ratio_shift = 26;

   size_t block_size = size_t(packing) << shift;
   size_t slab_last_count = slab_class->slab_last_size / block_size;
   this->slab_last_usables = get_usables_bits(slab_last_count);
}

address_t BlockPnS1Class::allocate(size_t target, MemoryContext* context) {
   _ASSERT(target <= this->getSizeMax());
   auto& bin = context->blocks_bins[this->binID];
   if (address_t block = bin.pop()) {
      return block;
   }
   bin.slabs = this->slab_class->allocate(context);
   bin.slabs->class_id = this->id;
   bin.slabs->block_ratio_shift = this->block_ratio_shift;
   if (bin.slabs->slab_position != this->slab_class->slab_per_batch) bin.slabs->usables = uint64_t(-1);
   else bin.slabs->usables = this->slab_last_usables;
   return bin.pop();
}

void BlockPnS1Class::receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) {
   auto& bin = context->blocks_bins[this->binID];
   slab->next = bin.slabs;
   bin.slabs = slab;
   _ASSERT(slab->next != slab);
}

void BlockPnS1Class::print() {
   int block_count = 1 << (32 - this->block_ratio_shift);
   int last_block_count = msb_64(this->slab_last_usables) + 1;
   printf("block %d [pns1] slab_count=%d-%d\n", this->getSizeMax(), block_count, last_block_count);
}

/*****************************************************
*
*  PnSn Class
*
******************************************************/

SlabPnSnClass::SlabPnSnClass(uint8_t id, uint8_t binID, uint8_t packing, uint8_t shift, uint8_t(&& layoutIDs)[8])
   : SlabClass(id), sizing(packing, shift) {
   this->binID = binID;
   this->slab_per_batch = 1 << (cPageSizeL2 - shift);
   memcpy(this->layoutIDs, &layoutIDs, 8);
}

tpSlabDescriptor SlabPnSnClass::allocate(MemoryContext* context) {
   auto& bin = context->space->slabs_bins[this->binID];
   if (auto slab = bin.pop(context->space, context->id)) {
      return slab;
   }

   address_t area = context->space->acquirePageSpan(this->sizing.packing, 0);
   auto batch = tpSlabBatchDescriptor(context->allocateSystemMemory(this->slab_per_batch + size_t(1)));
   memset(batch, 0, (this->slab_per_batch + size_t(1)) * 64);
   batch->class_id = this->id;
   batch->usables = get_usables_bits(this->slab_per_batch);
   batch->uses = 0;
   batch->length = this->slab_per_batch;
   batch->page_index = area.pageIndex;
   batch->gc_marks = 0;
   batch->gc_analyzis = 0;
   batch->next = bin.batches;
   bin.batches = batch;

   auto* entries = &context->space->regions[area.regionID]->pages_table[area.pageID];
   for (int i = 0; i < this->sizing.packing; i++) {
      auto& entry = entries[i];
      _ASSERT(entry.layoutID == 0 && entry.reference == 0);
      entry.reference = uint64_t(&batch[1]);
      entry.layoutID = this->layoutIDs[i];
      _ASSERT(entry.layoutID != 0);
   }

   return bin.pop(context->space, context->id);
}

void SlabPnSnClass::release(tpSlabDescriptor slab, MemoryContext* context) {
   auto batch = tpSlabBatchDescriptor(&slab[-slab->slab_position]);
   uint64_t slab_bit = uint64_t(1) << (slab->slab_position - 1);
   _ASSERT(batch->uses & slab_bit);
   batch->uses ^= slab_bit;
   if (batch->uses == 0) {
      printf("lost batch\n");
   }
}

BlockPnSnClass::BlockPnSnClass(uint8_t id, uint8_t binID, uint8_t packing, uint8_t shift, uint8_t block_per_slab_L2, SlabPnSnClass* slab_class)
   : BlockClass(id), sizing(packing, shift), slab_class(slab_class) {
   this->binID = binID;
   this->block_ratio_shift = 32 - block_per_slab_L2;
   this->block_usables = get_usables_bits(size_t(1) << block_per_slab_L2);
}

address_t BlockPnSnClass::allocate(size_t target, MemoryContext* context) {
   _ASSERT(target <= this->getSizeMax());
   auto& bin = context->blocks_bins[this->binID];
   if (address_t block = bin.pop()) {
      return block;
   }
   bin.slabs = this->slab_class->allocate(context);
   bin.slabs->class_id = this->id;
   bin.slabs->block_ratio_shift = this->block_ratio_shift;
   bin.slabs->usables = this->block_usables;
   return bin.pop();
}

void BlockPnSnClass::receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) {
   auto& bin = context->blocks_bins[this->binID];
   slab->next = bin.slabs;
   bin.slabs = slab;
}

void BlockPnSnClass::print() {
   printf("block %d [pnsn]\n", this->getSizeMax());
}

/*****************************************************
*
*  P1Sn Class
*
******************************************************/

SlabP1SnClass::SlabP1SnClass(uint8_t id, uint8_t packing, uint8_t shift, uint8_t(&& layoutIDs)[8])
   : SlabClass(id), sizing(packing, shift) {
   _ASSERT(shift == 16);
   memcpy(this->layoutIDs, &layoutIDs, 8);
}

tpSlabDescriptor SlabP1SnClass::allocate(MemoryContext* context) {
   address_t area = context->space->acquirePageSpan(this->sizing.packing, 0);

   auto slab = tpSlabDescriptor(context->allocateSystemMemory(1));
   slab->context_id = context->id;
   slab->class_id = 0;
   slab->block_ratio_shift = 0;
   slab->slab_position = 0;
   slab->page_index = area.pageIndex;
   slab->gc_marks = 0;
   slab->gc_analyzis = 0;
   slab->uses = 0;
   slab->usables = 0;
   slab->shared_freemap = 0;
   slab->next = 0;

   auto* entries = &context->space->regions[area.regionID]->pages_table[area.pageID];
   for (size_t i = 0; i < this->sizing.packing; i++) {
      auto& entry = entries[i];
      _ASSERT(entry.layoutID == 0 && entry.reference == 0);
      entry.reference = uint64_t(slab);
      entry.layoutID = this->layoutIDs[i];
   }

   return slab;
}

void SlabP1SnClass::release(tpSlabDescriptor slab, MemoryContext* context) {
   address_t addr = uint64_t(slab->page_index) << cPageSizeL2;
   context->space->releasePageSpan(addr, this->sizing.packing);
}

BlockP1SnClass::BlockP1SnClass(uint8_t id, uint8_t binID, uint8_t packing, uint8_t shift, uint8_t block_per_slab_L2, SlabP1SnClass* slab_class)
   : BlockClass(id), sizing(packing, shift) {
   this->binID = binID;
   this->block_ratio_shift = 32 - block_per_slab_L2;
   this->block_usables = get_usables_bits(size_t(1) << block_per_slab_L2);
   this->slab_class = slab_class;
}

address_t BlockP1SnClass::allocate(size_t target, MemoryContext* context) {
   _ASSERT(target <= this->getSizeMax());
   auto& bin = context->blocks_bins[this->binID];
   if (address_t block = bin.pop()) {
      return block;
   }
   bin.slabs = this->slab_class->allocate(context);
   bin.slabs->class_id = this->id;
   bin.slabs->block_ratio_shift = this->block_ratio_shift;
   bin.slabs->usables = this->block_usables;
   return bin.pop();
}

void BlockP1SnClass::receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) {
   auto& bin = context->blocks_bins[this->binID];
   slab->next = bin.slabs;
   bin.slabs = slab;
}

void BlockP1SnClass::print() {
   printf("block %d [p1sn]\n", this->getSizeMax());
}

/*****************************************************
*
*  Page-Span Class
*
******************************************************/

BlockPageSpanClass::BlockPageSpanClass(uint8_t id, uint8_t packing, uint16_t lengthL2)
   : BlockClass(id), packing(packing), lengthL2(lengthL2) {
}

address_t BlockPageSpanClass::allocate(size_t target, MemoryContext* context) {
   _ASSERT(target <= this->getSizeMax());
   address_t area = context->space->acquirePageSpan(this->packing, this->lengthL2 - cPageSizeL2);

   // Create a descriptor for this block
   auto slab = (tpSlabDescriptor)context->allocateSystemMemory(1);
   slab->context_id = context->id;
   slab->class_id = this->id;
   slab->uses = 1;
   slab->usables = 1;
   slab->slab_position = 0;
   slab->page_index = area.pageIndex;

   // Mark block pages in table
   auto* entries = &context->space->regions[area.regionID]->pages_table[area.pageID];
   auto entries_count = size_t(this->packing) << (this->lengthL2 - cPageSizeL2);
   for (uint32_t i = 0; i < entries_count; i++) {
      auto& entry = entries[i];
      _ASSERT(entry.layoutID == 0 && entry.reference == 0);
      entry.layoutID = 0;
      entry.reference = uintptr_t(slab);
   }
   return area;
}

void BlockPageSpanClass::receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) {
   address_t area = nullptr;
   area.pageIndex = slab->page_index;

   // Clean pages in table
   uint32_t entries_count = 0;
   auto* entries = &context->space->regions[area.regionID]->pages_table[area.pageID];
   while (entries[entries_count].reference == uintptr_t(slab)) {
      entries[entries_count].layoutID = 0;
      entries[entries_count].reference = 0;
      entries_count++;
   }

   // Release memory
   context->space->releasePageSpan(slab->page_index, entries_count);
   context->releaseSystemMemory(slab, 1);
}

size_t BlockPageSpanClass::getSizeMax() {
   return size_t(this->packing) << this->lengthL2;
}

void BlockPageSpanClass::print() {
   printf("block %d [sunit]\n", this->getSizeMax());
}

/*****************************************************
*
*  Unit-Span Class
*
******************************************************/

BlockUnitSpanClass::BlockUnitSpanClass(uint8_t id)
   : BlockClass(id) {
}

address_t BlockUnitSpanClass::allocate(size_t target, MemoryContext* context) {
   auto unit_count = alignX(target, ins::cUnitSize) >> ins::cUnitSizeL2;

   // Allocate unit span
   auto unit = context->space->acquireUnitSpan(unit_count);
   unit->commitMemorySpan();
   address_t area = unit->address();

   // Create a descriptor for this block
   auto slab = (tpSlabDescriptor)context->allocateSystemMemory(1);
   slab->context_id = context->id;
   slab->uses = 1;
   slab->usables = 1;
   slab->slab_position = 0;
   slab->page_index = area.pageIndex;

   // Mark pages in table
   auto* entries = &context->space->regions[area.regionID]->pages_table[area.pageID];
   auto entries_count = unit_count << cPagePerUnitL2;
   for (uint32_t i = 0; i < entries_count; i++) {
      auto& entry = entries[i];
      _ASSERT(entry.layoutID == 0 && entry.reference == 0);
      entry.layoutID = 0;
      entry.reference = uintptr_t(slab);
   }
   return area;
}

void BlockUnitSpanClass::receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) {
   address_t area = nullptr;
   area.pageIndex = slab->page_index;

   // Clean pages in table
   uint32_t entries_count = 0;
   auto* entries = &context->space->regions[area.regionID]->pages_table[area.pageID];
   while (entries[entries_count].reference == uintptr_t(slab)) {
      entries[entries_count].layoutID = 0;
      entries[entries_count].reference = 0;
      entries_count++;
   }

   // Release memory
   context->space->releasePageSpan(slab->page_index, entries_count);
   context->releaseSystemMemory(slab, 1);
}

void BlockUnitSpanClass::print() {
   printf("block 4Gb [unit]\n", this->getSizeMax());
}
