#include <ins/memory/contexts.h>
#include <ins/memory/schemas.h>
#include <typeinfo>

using namespace ins;
using namespace ins::mem;

sObjectSchema* mem::ObjectSchemas = 0;

void ManagedSchema::InstallSchema(const std::type_info& info, size_t size, ObjectTraverser traverser, ObjectFinalizer finalizer) {
   this->type_name = info.name();
   auto desc = mem::CreateObjectSchema(this, size, traverser, finalizer);
   this->id = mem::GetObjectSchemaID(desc);
}

size_t ManagedSchema::size() {
   return mem::GetObjectSchema(this->id)->base_size;
}

void traverse_aligned_bytes_range(BufferBytes ptr, size_t sz, mem::TraversalContext<sObjectSchema>* context) {
   auto cur = (BufferBytes)bit::align(uintptr_t(ptr), sizeof(void*));
   auto last = ptr + sz - sizeof(void*);
   while (cur <= last) {
      context->visit_ptr(context, *(void**)cur);
      cur += sizeof(void*);
   }
}

void traverse_unaligned_bytes_range(BufferBytes ptr, size_t sz, mem::TraversalContext<sObjectSchema>* context) {
   auto end = ptr + sz;
   for (auto cur = ptr; cur < end; cur++) {
      context->visit_ptr(context, *(void**)cur);
   }
}
