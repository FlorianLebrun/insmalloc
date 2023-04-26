#include <ins/memory/descriptors.h>
#include <ins/memory/map.h>
#include <ins/os/memory.h>
#include "./descriptors-allocator.h"
#include "./regions-allocator.h"

using namespace ins;
using namespace ins::mem;

BufferBytes mem::sDescriptorEntry::GetBuffer() {
   return BufferBytes(&this[1]);
}

size_t mem::sDescriptorEntry::GetBlockSizeL2(size_t size) {
   size_t sizeL2 = bit::log2_ceil_32(size);
   if (sizeL2 < 6) return 6;
   return sizeL2;
}

size_t mem::sDescriptorEntry::GetBufferSizeL2(size_t size) {
   return GetBlockSizeL2(size + sizeof(sDescriptorEntry));
}

BufferBytes mem::Descriptor::AllocateDescriptor(size_t size, size_t usedSize) {
   size_t sizeL2 = sDescriptorEntry::GetBufferSizeL2(size);

   size_t usedSizeL2 = 0;
   if (sizeL2 <= cst::PageSizeL2) usedSizeL2 = sizeL2;
   else if (size >= cst::PageSize) usedSizeL2 = sDescriptorEntry::GetBufferSizeL2(size);
   else usedSizeL2 = cst::PageSizeL2;

   DescriptorEntry result = space->descriptors_allocator.Allocate(sizeL2, usedSizeL2);
   _ASSERT(!result || result->sizeL2 == sizeL2);
   _ASSERT(!result || result->usedSizeL2 == 0 || result->usedSizeL2 == usedSizeL2);
   return result->GetBuffer();
}

mem::Descriptor::Descriptor() {
}

mem::Descriptor::Descriptor(uint8_t typeID) {
   if (auto header = this->GetEntry()) {
      header->typeID = typeID;
   }
}

DescriptorEntry mem::Descriptor::GetEntry() {
   auto loc = RegionLocation::New(this);
   if (loc.arena() && loc.layout() == RegionLayoutID::DescriptorHeapRegion) {
      return (DescriptorEntry)&BufferBytes(this)[-sizeof(sDescriptorEntry)];
   }
   return 0;
}

uint8_t mem::Descriptor::GetType() {
   if (auto entry = this->GetEntry()) {
      return entry->typeID;
   }
   return 0;
}

size_t mem::Descriptor::GetSize() {
   if (auto entry = this->GetEntry()) {
      auto sizeL2 = entry->sizeL2;
      return (size_t(1) << sizeL2) - sizeof(sDescriptorEntry);
   }
   return 0;
}

size_t mem::Descriptor::GetUsedSize() {
   if (auto entry = this->GetEntry()) {
      auto sizeL2 = entry->usedSizeL2;
      if (!sizeL2) sizeL2 = entry->sizeL2;
      return (size_t(1) << sizeL2) - sizeof(sDescriptorEntry);
   }
   return 0;
}

void mem::Descriptor::operator delete(void* ptr) {
   if (auto entry = ((Descriptor*)ptr)->GetEntry()) {
      space->descriptors_allocator.Dispose(entry);
   }
   else {
      printf("not deletable descriptor");
      exit(1);
   }
}

void mem::Descriptor::Resize(size_t newSize) {
   if (auto entry = this->GetEntry()) {
      auto usedSizeL2 = sDescriptorEntry::GetBufferSizeL2(newSize);
      space->descriptors_allocator.Extends(entry, usedSizeL2);
   }
   else {
      throw "not resizable descriptor";
   }
}
