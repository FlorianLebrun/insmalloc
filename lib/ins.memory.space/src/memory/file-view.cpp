#include <ins/memory/file-view.h>
#include <ins/memory/regions.h>

using namespace ins;
using namespace ins::mem;

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
namespace {
   typedef int NTSTATUS;

   typedef struct _UNICODE_STRING {
      USHORT Length;
      USHORT MaximumLength;
#ifdef MIDL_PASS
      [size_is(MaximumLength / 2), length_is((Length) / 2)] USHORT* Buffer;
#else // MIDL_PASS
      _Field_size_bytes_part_opt_(MaximumLength, Length) PWCH   Buffer;
#endif // MIDL_PASS
   } UNICODE_STRING;
   typedef UNICODE_STRING* PUNICODE_STRING;
   typedef const UNICODE_STRING* PCUNICODE_STRING;

   typedef struct _OBJECT_ATTRIBUTES {
      ULONG Length;
      HANDLE RootControllery;
      PUNICODE_STRING ObjectName;
      ULONG Attributes;
      PVOID SecurityDescriptor;        // Points to type SECURITY_DESCRIPTOR
      PVOID SecurityQualityOfService;  // Points to type SECURITY_QUALITY_OF_SERVICE
   } OBJECT_ATTRIBUTES;
   typedef OBJECT_ATTRIBUTES* POBJECT_ATTRIBUTES;
   typedef CONST OBJECT_ATTRIBUTES* PCOBJECT_ATTRIBUTES;

   typedef enum _SECTION_INHERIT {
      ViewShare = 1,
      ViewUnmap = 2
   } SECTION_INHERIT, * PSECTION_INHERIT;

   typedef NTSTATUS(NTAPI* fNtCreateSection)(
      OUT PHANDLE             SectionHandle,
      IN ULONG                DesiredAccess,
      IN POBJECT_ATTRIBUTES   ObjectAttributes OPTIONAL,
      IN PLARGE_INTEGER       MaximumSize OPTIONAL,
      IN ULONG                PageAttributess,
      IN ULONG                SectionAttributes,
      IN HANDLE               FileHandle OPTIONAL);

   typedef NTSTATUS(NTAPI* fNtMapViewOfSection)(
      IN HANDLE               SectionHandle,
      IN HANDLE               ProcessHandle,
      IN OUT PVOID* BaseAddress OPTIONAL,
      IN ULONG                ZeroBits OPTIONAL,
      IN ULONG                CommitSize,
      IN OUT PLARGE_INTEGER   SectionOffset OPTIONAL,
      IN OUT PSIZE_T          ViewSize,
      IN SECTION_INHERIT      InheritDisposition,
      IN ULONG                AllocationType OPTIONAL,
      IN ULONG                Protect);

   typedef NTSTATUS(NTAPI* fNtExtendSection)(
      IN HANDLE               SectionHandle,
      IN PLARGE_INTEGER       NewSectionSize);

   static fNtCreateSection NtCreateSection = 0;
   static fNtMapViewOfSection NtMapViewOfSection = 0;
   static fNtExtendSection NtExtendSection = 0;

   void __init_section_API() {
      static HMODULE hNtDll = 0;
      static std::mutex lock;
      if (hNtDll) return;
      std::lock_guard<std::mutex> guard(lock);
      if (hNtDll) return;
      hNtDll = LoadLibraryA("ntdll.dll");
      if (hNtDll) {
         NtCreateSection = fNtCreateSection(GetProcAddress(hNtDll, "NtCreateSection"));
         NtMapViewOfSection = fNtMapViewOfSection(GetProcAddress(hNtDll, "NtMapViewOfSection"));
         NtExtendSection = fNtExtendSection(GetProcAddress(hNtDll, "NtExtendSection"));
      }
   }
}

struct Win32DirectFileView : mem::DirectFileView {
   size_t view_size;
   size_t page_size;
   HANDLE hSection;
   std::mutex lock;
   bool ExtendSize(size_t new_size) override final {

      // Check if section extension shall be done
      if (new_size <= this->size) return true;
      new_size = bit::align(new_size, this->page_size);
      if (new_size > this->view_size)  return false;
      std::lock_guard<std::mutex> guard(lock);
      if (new_size <= this->size) return true;

      // Extend section
      LARGE_INTEGER SectionSize;
      SectionSize.QuadPart = LONGLONG(new_size);
      if (NtExtendSection(hSection, &SectionSize) >= 0) {
         this->size = new_size;
         return true;
      }
      return false;
   }
   size_t GetExtendSizeLimit()  override final {
      return this->view_size;
   }
   ~Win32DirectFileView() override {
      UnmapViewOfFile((LPVOID)this->base);
      CloseHandle(this->hSection);
      this->hSection = 0;
      this->base = 0;
      this->size = 0;
   }
};

using namespace ins;

address_t mem::DirectFileView::GetBase() {
   return this->base;
}

size_t mem::DirectFileView::GetSize() {
   return this->size;
}

FileBuffer mem::DirectFileView::MapBuffer(uint32_t offset, size_t size) {
   return FileBuffer(this->base + offset);
}

mem::DirectFileView* mem::DirectFileView::NewReadOnly(const char* filename) {
   __init_section_API();
   HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
   if (hFile != INVALID_HANDLE_VALUE) {
      SYSTEM_INFO info;
      GetSystemInfo(&info);

      // Adjust used size to file size
      ULARGE_INTEGER FileSize;
      FileSize.LowPart = GetFileSize(hFile, &FileSize.HighPart);
      size_t view_size = info.dwPageSize;
      if (view_size < FileSize.QuadPart) view_size = bit::align(FileSize.QuadPart, info.dwPageSize);

      // Create mapped section with fixed view size
      if (HANDLE hSection = CreateFileMapping(hFile, 0, PAGE_READONLY | SEC_RESERVE, 0, view_size, NULL)) {
         CloseHandle(hFile);

         PVOID BaseAddress = MapViewOfFile(hSection, FILE_MAP_READ, 0, 0, 0);
         if (BaseAddress) {
            auto view = Descriptor::New<Win32DirectFileView>();
            view->readOnly = true;
            view->hSection = hSection;
            view->base = address_t(BaseAddress);
            view->size = view_size;
            view->view_size = view_size;
            view->page_size = info.dwPageSize;
            return view;
         }
         else {
            CloseHandle(hSection);
         }
      }
   }
   CloseHandle(hFile);
   return 0;
}

mem::DirectFileView* mem::DirectFileView::NewReadWrite(const char* filename, size_t size, bool reset) {
   __init_section_API();
   HANDLE hFile = CreateFileA(filename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, reset ? CREATE_ALWAYS : OPEN_ALWAYS, 0, 0);
   if (hFile != INVALID_HANDLE_VALUE) {
      SYSTEM_INFO info;
      GetSystemInfo(&info);

      // Adjust used size to file granularity
      size_t view_size = bit::align(int64_t(size), info.dwPageSize);

      // Adjust used size to file size
      ULARGE_INTEGER FileSize;
      FileSize.LowPart = GetFileSize(hFile, &FileSize.HighPart);
      size_t used_size = info.dwPageSize;
      if (used_size < FileSize.QuadPart) used_size = bit::align(FileSize.QuadPart, info.dwPageSize);

      // Create section with only 1 page in the file
      HANDLE hSection = 0;
      LARGE_INTEGER SectionSize = { info.dwPageSize };
      auto status = NtCreateSection(&hSection,
         SECTION_EXTEND_SIZE | SECTION_MAP_READ | SECTION_MAP_WRITE, 0,
         &SectionSize, PAGE_READWRITE, SEC_COMMIT, hFile);
      CloseHandle(hFile);

      // Map section on the required view size
      if (status >= 0) {
         PVOID BaseAddress = 0;
         SIZE_T ViewSize = SIZE_T(view_size);
         auto status = NtMapViewOfSection(hSection, GetCurrentProcess(), &BaseAddress,
            0, 0, 0, &ViewSize, ViewUnmap, MEM_RESERVE, PAGE_READWRITE);
         if (status >= 0) {
            auto view = Descriptor::New<Win32DirectFileView>();
            view->readOnly = false;
            view->hSection = hSection;
            view->base = address_t(BaseAddress);
            view->size = SectionSize.QuadPart;
            view->view_size = view_size;
            view->page_size = info.dwPageSize;
            if (used_size > view->size) {
               if (!view->ExtendSize(used_size)) {
                  delete view;
                  return 0;
               }
            }
            return view;
         }
         else {
            CloseHandle(hSection);
         }
      }
   }
   CloseHandle(hFile);
   return false;
}
