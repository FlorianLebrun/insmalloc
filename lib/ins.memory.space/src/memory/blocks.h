#pragma once
#include <ins/memory/space.h>
#include <ins/memory/context.h>

namespace ins {

   /*****************************************************
   *
   *  PnS1 Class:
   *  - used for small block < 1024 bytes
   *  - use n slab of 1 page
   *  - slabs have 64 blocks, excepted the last one which is truncated
   *
   ******************************************************/
   struct SlabPnS1Class : SlabClass {
      uint8_t binID = -1;
      uint8_t layoutID;
      sizeid_t sizing;
      uint8_t slab_per_batch;
      uint16_t slab_last_size;
      SlabPnS1Class(uint8_t id, uint8_t binID, uint8_t layoutID, uint8_t packing, uint8_t shift);
      virtual tpSlabDescriptor allocate(MemoryContext* context) override final;
      virtual void release(tpSlabDescriptor slab, MemoryContext* context) override final;
      virtual sizeid_t getSlabSize() override { return this->sizing; }
   };
   struct BlockPnS1Class : BlockClass {
      uint8_t binID = -1;
      sizeid_t sizing;
      uint8_t block_ratio_shift;
      uint64_t slab_last_usables;
      SlabPnS1Class* slab_class;

      BlockPnS1Class(uint8_t id, uint8_t binID, uint8_t packing, uint8_t shift, SlabPnS1Class* slab_class);
      virtual address_t allocate(size_t target, MemoryContext* context) override final;
      virtual void receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) override final;
      virtual SlabClass* getSlabClass() override final { return slab_class; }
      virtual size_t getSizeMax() override { return this->sizing.size(); }
      virtual sizeid_t getBlockSize() override { return this->sizing; }
      virtual void print() override final;
   };

   /*****************************************************
   *
   *  PnSn Class:
   *  - used for medium block >= 1024 and < 114k bytes
   *  - use n slab on n page
   *
   ******************************************************/
   struct SlabPnSnClass : SlabClass {
      uint8_t binID = -1;
      sizeid_t sizing;
      uint8_t slab_per_batch;
      uint8_t layoutIDs[8];
      SlabPnSnClass(uint8_t id, uint8_t binID, uint8_t packing, uint8_t shift, uint8_t(&& layoutIDs)[8]);
      virtual tpSlabDescriptor allocate(MemoryContext* context) override final;
      virtual void release(tpSlabDescriptor slab, MemoryContext* context) override final;
      virtual sizeid_t getSlabSize() override { return this->sizing; }
   };
   struct BlockPnSnClass : BlockClass {
      uint8_t binID = -1;
      sizeid_t sizing;
      uint8_t block_ratio_shift;
      uint64_t block_usables;
      SlabPnSnClass* slab_class;
      BlockPnSnClass(uint8_t id, uint8_t binID, uint8_t packing, uint8_t shift, uint8_t block_per_slab_L2, SlabPnSnClass* slab_class);
      virtual address_t allocate(size_t target, MemoryContext* context) override final;
      virtual void receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) override final;
      virtual SlabClass* getSlabClass() override final { return slab_class; }
      virtual size_t getSizeMax() override { return this->sizing.size(); }
      virtual sizeid_t getBlockSize() override { return this->sizing; }
      virtual void print() override final;
   };

   /*****************************************************
   *
   *  P1Sn Class
   *  - used for medium block >= 1024 and < 229k bytes
   *  - use one slab on n page
   *
   ******************************************************/
   struct SlabP1SnClass : SlabClass {
      sizeid_t sizing;
      uint8_t layoutIDs[8];
      SlabP1SnClass(uint8_t id, uint8_t packing, uint8_t shift, uint8_t(&& layoutIDs)[8]);
      virtual tpSlabDescriptor allocate(MemoryContext* context) override final;
      virtual void release(tpSlabDescriptor slab, MemoryContext* context) override final;
      virtual sizeid_t getSlabSize() override { return this->sizing; }
   };
   struct BlockP1SnClass : BlockClass {
      uint8_t binID = -1;
      sizeid_t sizing;
      uint8_t block_ratio_shift;
      uint64_t block_usables;
      SlabP1SnClass* slab_class;
      BlockP1SnClass(uint8_t id, uint8_t binID, uint8_t packing, uint8_t shift, uint8_t block_per_slab_L2, SlabP1SnClass* slab_class);
      virtual address_t allocate(size_t target, MemoryContext* context) override final;
      virtual void receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) override final;
      virtual SlabClass* getSlabClass() override final { return slab_class; }
      virtual size_t getSizeMax() override { return this->sizing.size(); }
      virtual sizeid_t getBlockSize() override { return this->sizing; }
      virtual void print() override final;
   };

   /*****************************************************
   *
   *  Page-Span Class:
   *  - used for large block >= 64k and < 14M bytes
   *  - use one slab on n page
   *
   ******************************************************/
   struct BlockPageSpanClass : BlockClass {
      uint8_t packing;
      uint16_t lengthL2;
      BlockPageSpanClass(uint8_t id, uint8_t packing, uint16_t lengthL2);
      virtual address_t allocate(size_t target, MemoryContext* context) override final;
      virtual void receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) override final;
      virtual SlabClass* getSlabClass() override final { return 0; }
      virtual size_t getSizeMax() override final;
      virtual void print() override final;
   };

   /*****************************************************
   *
   *  Unit-Span Class:
   *  - used for huge block >= 4Mb and < 4Gb bytes
   *  - use one slab on n unit
   *
   ******************************************************/
   struct BlockUnitSpanClass : BlockClass {
      BlockUnitSpanClass(uint8_t id);
      virtual address_t allocate(size_t target, MemoryContext* context) override final;
      virtual void receivePartialSlab(tpSlabDescriptor slab, MemoryContext* context) override final;
      virtual SlabClass* getSlabClass() override final { return 0; }
      virtual size_t getSizeMax() { return size_t(1) << 31; }
      virtual void print() override final;
   };

   void printAllBlocks();
}
