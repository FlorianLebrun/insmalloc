#include <ins/memory/descriptors.h>
#include <ins/memory/regions.h>
#include <ins/memory/space.h>
#include <ins/os/memory.h>

using namespace ins;

static DescriptorType types[64] = {
   {0, "(undefined)"},
   {1, "(free)"},
   {2, "DescriptorHeap"},
   {3, "Arena"},
   {4, "Region"},
};

// Descriptor heap (based on buddy allocator)
struct DescriptorsHeap : sDescriptorEntry {

   struct DescriptorsArena : ArenaDescriptor {
      DescriptorsArena() : ArenaDescriptor(cst::ArenaSizeL2) {
         this->availables_count--;
         _ASSERT(this->availables_count == 0);
      }
   };

   typedef struct sBlockDescriptor : sDescriptorEntry {
      sBlockDescriptor** bucketAnchor, * bucketNext;
      sBlockDescriptor* Reset(uint8_t sizeL2) {
         this->typeID = DescriptorTypeID::FreeBlock;
         this->sizeL2 = sizeL2;
         this->usedSizeL2 = 0;
         this->bucketAnchor = 0;
         this->bucketNext = 0;
         return this;
      }
   } *BlockDescriptor;
   static_assert(sizeof(sBlockDescriptor) <= 64, "bad size");

   typedef struct sPageSpanDescriptor : sDescriptorEntry {
      sPageSpanDescriptor** bucketAnchor, * bucketNext;
      sPageSpanDescriptor* paging_L, * paging_R;
      uint8_t paging_H;
      struct AVLHandler {
         typedef sPageSpanDescriptor* Node;
         typedef AVLOperators<sPageSpanDescriptor, AVLHandler, intptr_t> Operators;
         static Node& left(Node node) { return node->paging_L; }
         static Node& right(Node node) { return node->paging_R; }
         static uint8_t& height(Node node) { return node->paging_H; }
         struct Insert : Operators::IInsertable {
            Node node; Insert(Node node) : node(node) {}
            virtual intptr_t compare(Node node) override {
               if (auto c = intptr_t(this->node->ptr) - intptr_t(node->ptr)) return c;
               return 0;
            }
            virtual Node create(Node overridden) override {
               if (overridden) throw; return this->node;
            }
         };
         struct Key : Operators::IComparable {
            uintptr_t ptr; Key(uintptr_t ptr) : ptr(ptr) {}
            virtual intptr_t compare(Node node) override {
               if (auto c = intptr_t(this->ptr) - intptr_t(node->ptr)) return c;
               return 0;
            }
         };
      };
      uint16_t lengthL2;
      uintptr_t ptr;
      sPageSpanDescriptor* Reset(uintptr_t ptr, uint16_t lengthL2) {
         this->typeID = DescriptorTypeID::FreeSpan;
         this->ptr = ptr;
         this->lengthL2 = lengthL2;
         this->bucketAnchor = 0;
         this->bucketNext = 0;
         this->paging_L = 0;
         this->paging_R = 0;
         this->paging_H = 0;
         return this;
      }
   } *PageSpanDescriptor;
   static_assert(sizeof(sPageSpanDescriptor) <= 64, "bad size");

   struct BlockDescriptorBucket {
      static const size_t cBlockSizeL2_Min = 6;
      static const size_t cBlockSizeL2_Max = cst::PageSizeL2;

      uint32_t sizesMap = 0; // bit: 1 -> contains block, 0 -> empty
      BlockDescriptor blocks[cst::PageSizeL2 + 1];

      void PushBlock(BlockDescriptor block) {
         auto& anchor = blocks[block->sizeL2];
         block->bucketAnchor = &anchor;
         block->bucketNext = anchor;
         anchor = block;
         sizesMap |= uint32_t(1) << block->sizeL2;
      }
      BlockDescriptor PullBlock(size_t minSizeL2) {
         auto fsizeL2 = GetAvailableSizeL2(minSizeL2, sizesMap);
         if (fsizeL2 > 0) {
            auto block = blocks[fsizeL2];
            block->bucketAnchor[0] = block->bucketNext;
            if (!blocks[fsizeL2]) {
               sizesMap ^= uint32_t(1) << fsizeL2;
            }
            return block;
         }
         return 0;
      }
      BlockDescriptor MakeBlock(size_t sizeL2) {
         if (auto block = PullBlock(sizeL2)) {
            this->SliceBlockBuffer(block, sizeL2);
            return block;
         }
         return 0;
      }
      void SliceBlockBuffer(DescriptorEntry buffer, size_t toSizeL2) {
         while (buffer->sizeL2 > toSizeL2) {
            auto sizeL2 = buffer->sizeL2 - 1;
            auto buddyBit = uintptr_t(1) << sizeL2;
            auto buddy = BlockDescriptor(ptr_t(uintptr_t(buffer) ^ buddyBit))->Reset(sizeL2);
            buffer->sizeL2 = sizeL2;
            this->PushBlock(buddy);
         }
      }
      void MergeBlocks() {
         // Bubble merging algorithm, iterate from small to big size
         for (auto sizeL2 = cBlockSizeL2_Min; sizeL2 < cBlockSizeL2_Max; sizeL2++) {

            // Merge blocks
            auto block = blocks[sizeL2];
            auto buddyBit = uintptr_t(1) << sizeL2;
            while (block) {
               auto buddy = BlockDescriptor(uintptr_t(block) ^ buddyBit);
               if (buddy->typeID == DescriptorTypeID::FreeBlock && buddy->sizeL2 == sizeL2) {
                  // Unlink buddies
                  buddy->bucketAnchor[0] = buddy->bucketNext;
                  block->bucketAnchor[0] = block->bucketNext;
                  auto nextBlock = block->bucketNext;

                  // Push upper block
                  if (uintptr_t(block) & buddyBit) block = buddy;
                  block->sizeL2++;
                  this->PushBlock(block);

                  block = nextBlock;
               }
               else {
                  block = block->bucketNext;
               }
            }

            // Update sizes map
            if (!blocks[sizeL2]) {
               sizesMap &= ~(uint32_t(1) << sizeL2);
            }
         }
      }
   };

   struct PageSpanDescriptorBucket {
      typedef sPageSpanDescriptor::AVLHandler AVL;
      uint32_t lengthsMap = 0; // bit: 1 -> contains span, 0 -> empty
      PageSpanDescriptor paging = 0; // Unused page span map (in AVL Tree)
      PageSpanDescriptor spans[cst::SpaceSizeL2 - cst::PageSizeL2];

      void PushSpan(PageSpanDescriptor span) {
         auto& anchor = this->spans[span->lengthL2];
         span->bucketAnchor = &anchor;
         span->bucketNext = anchor;
         anchor = span;
         this->lengthsMap |= uint32_t(1) << span->lengthL2;
         this->paging = AVL::Operators::insertAt(this->paging, &AVL::Insert(span), span);
      }
      PageSpanDescriptor PullSpan(size_t minLengthL2) {
         auto flengthL2 = GetAvailableSizeL2(minLengthL2, this->lengthsMap);
         if (flengthL2 >= 0) {
            auto span = this->spans[flengthL2];
            span->bucketAnchor[0] = span->bucketNext;
            this->paging = AVL::Operators::removeAt(this->paging, &AVL::Key(span->ptr), span);
            if (!this->spans[flengthL2]) {
               this->lengthsMap ^= uint32_t(1) << flengthL2;
            }
            return span;
         }
         return 0;
      }
      uintptr_t MakeSpan(size_t lengthL2, BlockDescriptorBucket& blocks) {
         if (auto span = this->PullSpan(lengthL2)) {
            auto spanPtr = span->ptr;
            blocks.PushBlock(BlockDescriptor(span)->Reset(6));
            return this->SlicePageSpan(spanPtr, span->lengthL2, lengthL2, blocks);
         }
         return 0;
      }
      uintptr_t SlicePageSpan(uintptr_t spanPtr, size_t fromLengthL2, size_t toLengthL2, BlockDescriptorBucket& blocks) {
         while (fromLengthL2 > toLengthL2) {
            auto buddyBit = cst::PageSize << (--fromLengthL2);
            auto buddy = PageSpanDescriptor(blocks.MakeBlock(6))->Reset(spanPtr | buddyBit, fromLengthL2);
            this->PushSpan(buddy);
         }
         return spanPtr;
      }
      void FeedBlocksBucket(BlockDescriptorBucket& blocks) {
         if (auto span = this->PullSpan(0)) {
            auto spanPtr = span->ptr;
            OSMemory::CommitMemory(spanPtr, cst::PageSize);
            blocks.PushBlock(BlockDescriptor(spanPtr)->Reset(cst::PageSizeL2));
            this->SlicePageSpan(spanPtr, span->lengthL2, 0, blocks);
         }
      }
   };

   std::mutex lock;
   BlockDescriptorBucket blocks;
   PageSpanDescriptorBucket spans;
   DescriptorsArena arena;
   uint32_t lengthL2 = 0;

   static DescriptorsHeap* DescriptorsHeap::New(size_t countL2) {
      size_t count = size_t(1) << countL2;
      address_t buffer = ins::RegionsHeap.ReserveArena();
      OSMemory::CommitMemory(buffer, cst::PageSize);
      auto region = new((void*)buffer) DescriptorsHeap(countL2);
      region->arena.indice = buffer.arenaID;
      ins::RegionsHeap.arenas[buffer.arenaID] = ArenaEntry(&region->arena);
      RegionLocation::New(region).layout() = RegionLayoutID::DescriptorHeapRegion;
      return region;
   }
   DescriptorEntry Allocate(size_t sizeL2, size_t usedSizeL2 = 0) {
      std::lock_guard<std::mutex> guard(this->lock);
      if (sizeL2 < cst::PageSizeL2) {
         auto block = this->blocks.MakeBlock(sizeL2);
         if (!block) {
            this->spans.FeedBlocksBucket(this->blocks);
            block = this->blocks.MakeBlock(sizeL2);
         }
         _ASSERT(block);
         block->typeID = 0;
         return block;
      }
      else {
         _ASSERT(usedSizeL2 >= cst::PageSizeL2);
         auto lengthL2 = sizeL2 - cst::PageSizeL2;
         auto spanPtr = this->spans.MakeSpan(lengthL2, this->blocks);
         OSMemory::CommitMemory(spanPtr, size_t(1) << usedSizeL2);
         auto entry = DescriptorEntry(spanPtr);
         entry->typeID = 0;
         entry->sizeL2 = sizeL2;
         entry->usedSizeL2 = usedSizeL2;
         return entry;
      }
   }
   void Extends(DescriptorEntry entry, size_t usedSizeL2) {
      if (entry->sizeL2 < usedSizeL2) {
         throw "overflow";
      }
      if (entry->usedSizeL2 <= usedSizeL2) {
         auto newSize = size_t(1) << usedSizeL2;
         OSMemory::CommitMemory(uintptr_t(entry), newSize);
      }
      else {
         auto newSize = size_t(1) << usedSizeL2;
         auto prevSize = size_t(1) << entry->usedSizeL2;
         OSMemory::DecommitMemory(uintptr_t(entry) + newSize, prevSize - newSize);
      }
      entry->usedSizeL2 = usedSizeL2;
   }
   void Dispose(DescriptorEntry entry) {
      std::lock_guard<std::mutex> guard(this->lock);
      auto sizeL2 = entry->sizeL2;
      if (sizeL2 < cst::PageSizeL2) {
         this->blocks.PushBlock(BlockDescriptor(entry)->Reset(sizeL2));
      }
      else {
         OSMemory::DecommitMemory(uintptr_t(entry), size_t(1) << entry->usedSizeL2);
         auto lengthL2 = sizeL2 - cst::PageSizeL2;
         auto span = PageSpanDescriptor(this->blocks.MakeBlock(6));
         this->spans.PushSpan(span->Reset(uintptr_t(entry), lengthL2));
      }
   }
private:
   DescriptorsHeap(uint32_t countL2) {
      this->typeID = DescriptorTypeID::DescriptorHeap;
      this->sizeL2 = cst::PageSizeL2;
      this->usedSizeL2 = 0;

      // Register committed page
      index_t descSizeL2 = GetBlockSizeL2(sizeof(*this));
      this->blocks.SliceBlockBuffer(this, descSizeL2);

      // Register free page spans
      this->lengthL2 = countL2 + cst::ArenaSizeL2 - cst::PageSizeL2;
      this->spans.SlicePageSpan(uintptr_t(this), this->lengthL2, 0, blocks);
   }
   static int32_t GetAvailableSizeL2(size_t sizeL2, uint32_t sizesMap) {
      auto bitmap = sizesMap & umask_32(sizeL2);
      if (!bitmap) return -1;
      return lsb_32(bitmap);
   }
};

static DescriptorsHeap* descriptorsHeap = 0;

namespace ins {
   void DescriptorsHeap__init__() {
      _ASSERT(descriptorsHeap == 0);
      descriptorsHeap = DescriptorsHeap::New(0);
   }
}

BufferBytes ins::sDescriptorEntry::GetBuffer() {
   return BufferBytes(&this[1]);
}

size_t ins::sDescriptorEntry::GetBlockSizeL2(size_t size) {
   size_t sizeL2 = log2_ceil_32(size);
   if (sizeL2 < 6) return 6;
   return sizeL2;
}

size_t ins::sDescriptorEntry::GetBufferSizeL2(size_t size) {
   return GetBlockSizeL2(size + sizeof(sDescriptorEntry));
}

BufferBytes ins::Descriptor::AllocateDescriptor(size_t size, size_t usedSize) {
   size_t sizeL2 = sDescriptorEntry::GetBufferSizeL2(size);

   size_t usedSizeL2 = 0;
   if (sizeL2 <= cst::PageSizeL2) usedSizeL2 = sizeL2;
   else if (size >= cst::PageSize) usedSizeL2 = sDescriptorEntry::GetBufferSizeL2(size);
   else usedSizeL2 = cst::PageSizeL2;

   DescriptorEntry result = descriptorsHeap->Allocate(sizeL2, usedSizeL2);
   _ASSERT(!result || result->sizeL2 == sizeL2);
   _ASSERT(!result || result->usedSizeL2 == 0 || result->usedSizeL2 == usedSizeL2);
   return result->GetBuffer();
}

ins::Descriptor::Descriptor() {
}

ins::Descriptor::Descriptor(uint8_t typeID) {
   if (auto header = this->GetEntry()) {
      header->typeID = typeID;
   }
}

DescriptorEntry ins::Descriptor::GetEntry() {
   auto loc = RegionLocation::New(this);
   if (loc.arena() && loc.layout() == RegionLayoutID::DescriptorHeapRegion) {
      return (DescriptorEntry)&BufferBytes(this)[-sizeof(sDescriptorEntry)];
   }
   return 0;
}

uint8_t ins::Descriptor::GetType() {
   if (auto entry = this->GetEntry()) {
      return entry->typeID;
   }
   return 0;
}

size_t ins::Descriptor::GetSize() {
   if (auto entry = this->GetEntry()) {
      auto sizeL2 = entry->sizeL2;
      return (size_t(1) << sizeL2) - sizeof(sDescriptorEntry);
   }
   return 0;
}

size_t ins::Descriptor::GetUsedSize() {
   if (auto entry = this->GetEntry()) {
      auto sizeL2 = entry->usedSizeL2;
      if (!sizeL2) sizeL2 = entry->sizeL2;
      return (size_t(1) << sizeL2) - sizeof(sDescriptorEntry);
   }
   return 0;
}

void ins::Descriptor::operator delete(void* ptr) {
   if (auto entry = ((Descriptor*)ptr)->GetEntry()) {
      descriptorsHeap->Dispose(entry);
   }
   else {
      printf("not deletable descriptor");
      exit(1);
   }
}

void ins::Descriptor::Resize(size_t newSize) {
   if (auto entry = this->GetEntry()) {
      auto usedSizeL2 = sDescriptorEntry::GetBufferSizeL2(newSize);
      descriptorsHeap->Extends(entry, usedSizeL2);
   }
   else {
      throw "not resizable descriptor";
   }
}
