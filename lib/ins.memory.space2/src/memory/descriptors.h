#include <stdint.h>
#include <stdlib.h>
#include <mutex>
#include <ins/binary/alignment.h>
#include <ins/binary/bitwise.h>
#include <ins/avl/AVLOperators.h>
#include "./structs.h"
#include "../os/memory.h"

namespace ins {

   enum class DescriptorType {
      Free,
      System,
   };

   typedef struct sDescriptor {
      virtual size_t GetSize() = 0;
      virtual DescriptorType GetType() { return DescriptorType::System; }
   } *Descriptor;

   typedef struct sExtensibleDescriptor : sDescriptor {
      virtual void SetUsedSize(size_t commited) = 0;
      virtual size_t GetUsedSize() = 0;
   } *ExtensibleDescriptor;

   typedef struct sBlockDescriptor : sDescriptor {
      sBlockDescriptor** bucketAnchor = 0, * bucketNext = 0;
      uint32_t sizeL2 = 0;
      sBlockDescriptor(uint32_t sizeL2) : sizeL2(sizeL2) {}
      DescriptorType GetType() override { return DescriptorType::Free; }
      size_t GetSize() override { return size_t(1) << sizeL2; }
   } *BlockDescriptor;
   static_assert(sizeof(sBlockDescriptor) <= 64, "bad size");

   typedef struct sPageSpanDescriptor : sDescriptor {
      sPageSpanDescriptor** bucketAnchor = 0, * bucketNext = 0;
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
      sPageSpanDescriptor(uintptr_t ptr, uint32_t lengthL2) : ptr(ptr), lengthL2(lengthL2) { }
      size_t GetSize() override { return sizeof(sPageSpanDescriptor); }
   } *PageSpanDescriptor;
   static_assert(sizeof(sPageSpanDescriptor) <= 64, "bad size");

   // Descriptor allocator (based on buddy allocator)
   struct sDescriptorRegion : sDescriptor {

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
               SliceBlockBuffer(block, block->sizeL2, sizeL2);
               return block;
            }
            return 0;
         }
         void SliceBlockBuffer(ptr_t buffer, size_t fromSizeL2, size_t toSizeL2) {
            while (fromSizeL2 > toSizeL2) {
               auto buddyBit = uintptr_t(1) << (--fromSizeL2);
               auto buddy = new(ptr_t(uintptr_t(buffer) ^ buddyBit)) sBlockDescriptor(fromSizeL2);
               PushBlock(buddy);
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
                  if (buddy->GetType() == DescriptorType::Free && buddy->sizeL2 == sizeL2) {
                     // Unlink buddies
                     buddy->bucketAnchor[0] = buddy->bucketNext;
                     block->bucketAnchor[0] = block->bucketNext;
                     auto nextBlock = block->bucketNext;

                     // Push upper block
                     if (uintptr_t(block) & buddyBit) block = buddy;
                     block->sizeL2++;
                     PushBlock(block);

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
               blocks.PushBlock(new(span) sBlockDescriptor(6));
               return SlicePageSpan(spanPtr, span->lengthL2, lengthL2, blocks);
            }
            return 0;
         }
         uintptr_t SlicePageSpan(uintptr_t spanPtr, size_t fromLengthL2, size_t toLengthL2, BlockDescriptorBucket& blocks) {
            while (fromLengthL2 > toLengthL2) {
               auto buddyBit = cst::PageSize << (--fromLengthL2);
               auto buddy = new(blocks.MakeBlock(6)) sPageSpanDescriptor(spanPtr | buddyBit, fromLengthL2);
               PushSpan(buddy);
            }
            return spanPtr;
         }
         void FeedBlocksBucket(BlockDescriptorBucket& blocks) {
            if (auto span = this->PullSpan(0)) {
               auto spanPtr = span->ptr;
               OSMemory::CommitMemory(spanPtr, cst::PageSize);
               blocks.PushBlock(new(ptr_t(spanPtr)) sBlockDescriptor(cst::PageSizeL2));
               SlicePageSpan(spanPtr, span->lengthL2, 0, blocks);
            }
         }
      };

      std::mutex lock;
      BlockDescriptorBucket blocks;
      PageSpanDescriptorBucket spans;
      uint32_t lengthL2 = 0;

      sDescriptorRegion(uint32_t sizeL2) {

         // Register committed page
         index_t descSizeL2 = GetBlockSizeL2(sizeof(*this));
         blocks.SliceBlockBuffer(this, cst::PageSizeL2, descSizeL2);

         // Register free page spans
         this->lengthL2 = sizeL2 - cst::PageSizeL2;
         spans.SlicePageSpan(uintptr_t(this), this->lengthL2, 0, blocks);
      }
      size_t GetSize() override {
         return 0;
      }
      sDescriptor* GetDescriptorAt(index_t index) {
         return 0;
      }
      template<typename T, typename ...Targ>
      T* New(Targ... args) {
         auto sizeL2 = GetBlockSizeL2(sizeof(T));
         auto result = new(this->Allocate(sizeL2, sizeL2)) T(args...);
         _ASSERT(!result || result->GetSize() == sizeof(T));
         return result;
      }
      template<typename T, typename ...Targ>
      T* NewExtensible(size_t size, Targ... args) {
         auto sizeL2 = GetBlockSizeL2(size);

         size_t usedSizeL2 = 0;
         if (sizeL2 <= cst::PageSizeL2) usedSizeL2 = sizeL2;
         else if (sizeof(T) >= cst::PageSize) usedSizeL2 = GetBlockSizeL2(sizeof(T));
         else usedSizeL2 = cst::PageSizeL2;

         auto result = new(this->Allocate(sizeL2, usedSizeL2)) T(size_t(1) << usedSizeL2, size_t(1) << sizeL2, args...);
         _ASSERT(!result || result->GetSize() >= size);
         _ASSERT(!result || result->GetUsedSize() >= (1 << usedSizeL2));
         return result;
      }
      ptr_t Allocate(size_t sizeL2, size_t usedSizeL2 = 0) {
         std::lock_guard<std::mutex> guard(this->lock);
         if (sizeL2 < cst::PageSizeL2) {
            auto block = this->blocks.MakeBlock(sizeL2);
            if (!block) {
               this->spans.FeedBlocksBucket(this->blocks);
               block = this->blocks.MakeBlock(sizeL2);
            }
            _ASSERT(block);
            return block;
         }
         else {
            _ASSERT(usedSizeL2 >= cst::PageSizeL2);
            auto lengthL2 = sizeL2 - cst::PageSizeL2;
            auto spanPtr = this->spans.MakeSpan(lengthL2, this->blocks);
            OSMemory::CommitMemory(spanPtr, size_t(1) << usedSizeL2);
            return ptr_t(spanPtr);
         }
      }
      void Extends(sExtensibleDescriptor* entry, size_t extendedSize) {
         auto sizeL2 = GetBlockSizeL2(extendedSize);
         OSMemory::CommitMemory(uintptr_t(entry), size_t(1) << sizeL2);
         entry->SetUsedSize(size_t(1) << sizeL2);
         _ASSERT(entry->GetUsedSize() == (size_t(1) << sizeL2));
      }
      void Dispose(sDescriptor* entry) {
         std::lock_guard<std::mutex> guard(this->lock);
         auto sizeL2 = GetBlockSizeL2(entry->GetSize());
         if (sizeL2 < cst::PageSizeL2) {
            blocks.PushBlock(new(entry) sBlockDescriptor(sizeL2));
         }
         else {
            OSMemory::DecommitMemory(uintptr_t(entry), 1 << sizeL2);
            auto lengthL2 = sizeL2 - cst::PageSizeL2;
            auto span = new(this->blocks.MakeBlock(6)) sPageSpanDescriptor(uintptr_t(entry), lengthL2);
            spans.PushSpan(span);
         }
      }
   private:
      static size_t GetBlockSizeL2(size_t size) {
         size_t sizeL2 = log2_ceil_32(size);
         if (sizeL2 < 6) return 6;
         return sizeL2;
      }
      static int32_t GetAvailableSizeL2(size_t sizeL2, uint32_t sizesMap) {
         auto bitmap = sizesMap & umask_32(sizeL2);
         if (!bitmap) return -1;
         return lsb_32(bitmap);
      }
   };
}