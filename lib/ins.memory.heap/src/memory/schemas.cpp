#include <ins/memory/contexts.h>
#include <ins/memory/schemas.h>
#include <ins/memory/space.h>
#include <ins/os/memory.h>
#include <typeinfo>

using namespace ins;
using namespace ins::mem;

SchemasHeap mem::schemasHeap;

SchemasHeap::SchemasHeap() {
   address_t buffer = mem::Regions.ReserveArena();
   this->arena.indice = buffer.arenaID;
   this->arenaBase = buffer;
   this->alloc_cursor = buffer + sizeof(SchemaDesc);
   this->alloc_commited = buffer;
   this->alloc_end = buffer + cst::ArenaSize;
   this->alloc_region_size = size_t(1) << 16;
   mem::Regions.ArenaMap[buffer.arenaID] = ArenaEntry(&this->arena);
}

SchemaDesc* SchemasHeap::CreateSchema(ISchemaInfos* infos, uint32_t base_size, ObjectTraverser traverser) {
   std::lock_guard<std::mutex> guard(this->lock);
   auto schema = (SchemaDesc*)this->alloc_cursor;
   this->alloc_cursor += sizeof(SchemaDesc);
   if (this->alloc_cursor > this->alloc_commited) {
      auto region_base = this->alloc_commited;
      this->alloc_commited += this->alloc_region_size;
      if (
         this->alloc_commited > this->alloc_end ||
         !os::CommitMemory(region_base, this->alloc_region_size))
      {
         throw "OOM";
         exit(1);
      }
   }
   schema->traverser = traverser;
   schema->infos = infos;
   schema->base_size = base_size;
   return schema;
}

void ManagedSchema::load(const std::type_info& info, size_t size, ObjectTraverser traverser) {
   this->type_name = info.name();
   auto desc = mem::schemasHeap.CreateSchema(this, size, traverser);
   this->id = mem::schemasHeap.GetSchemaID(desc);
}

size_t ManagedSchema::size() {
   return mem::schemasHeap.GetSchemaDesc(this->id)->base_size;
}

void traverse_aligned_bytes_range(BufferBytes ptr, size_t sz, mem::TraversalContext<SchemaDesc>* context) {
   auto cur = (BufferBytes)bit::align(uintptr_t(ptr), sizeof(void*));
   auto last = ptr + sz - sizeof(void*);
   while (cur <= last) {
      context->visit_ptr(context, *(void**)cur);
      cur += sizeof(void*);
   }
}

void traverse_unaligned_bytes_range(BufferBytes ptr, size_t sz, mem::TraversalContext<SchemaDesc>* context) {
   auto end = ptr + sz;
   for (auto cur = ptr; cur < end; cur++) {
      context->visit_ptr(context, *(void**)cur);
   }
}
