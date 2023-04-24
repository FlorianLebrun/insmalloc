#pragma once
#include <ins/memory/descriptors.h>

namespace ins::mem {

   struct FileBuffer {
      FileBuffer(uintptr_t base) {
      }
      ~FileBuffer() {
      }
   };

   struct FileView : Descriptor {
      virtual ~FileView() {}
      virtual size_t GetSize() = 0;
      virtual size_t GetExtendSizeLimit() = 0;
      virtual bool ExtendSize(size_t size) = 0;
      virtual FileBuffer MapBuffer(uint32_t offset, size_t size) = 0;
   };

   struct DirectFileView : FileView {
   protected:
      uintptr_t base;
      size_t size;
      bool readOnly;
   public:
      address_t GetBase();
      size_t GetSize() override final;
      FileBuffer MapBuffer(uint32_t offset, size_t size) override final;
      static DirectFileView* NewReadOnly(const char* filename);
      static DirectFileView* NewReadWrite(const char* filename, size_t size, bool reset = false);
   };

}