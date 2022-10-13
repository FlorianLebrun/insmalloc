#include <ins/memory/heap.h>
#include <ins/memory/schemas.h>
#include <typeinfo>

using namespace ins;

SchemasHeap ins::schemasHeap;

namespace ins {

   struct ManagedSchema : ISchemaInfos {
      SchemaID id = 0;
      const char* type_name = 0;
      const char* name() override { return this->type_name; }
      void load(const std::type_info& info, size_t size, ObjectTraverser traverser);
      size_t size();
   };

   template<typename T>
   struct ManagedClass {
      static ManagedSchema schema;
      void* operator new(size_t size) {
         if (schema.id == 0) schema.load(typeid(T), sizeof(T), ObjectTraverser(T::__traverser__));
         _ASSERT(schema.size() == size);
         return &ins_malloc_schema(schema.id)[1];
      }
   };
}

SchemasHeap::SchemasHeap() {
   address_t buffer = MemorySpace::ReserveArena();
   this->arena.base = buffer;
   this->alloc_cursor = buffer + sizeof(SchemaDesc);
   this->alloc_commited = buffer;
   this->alloc_end = buffer + cst::ArenaSize;
   this->alloc_region_size = size_t(1) << 16;
   MemorySpace::state.table[buffer.arenaID] = ArenaEntry(&this->arena);
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
         !OSMemory::CommitMemory(region_base, this->alloc_region_size))
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
   auto desc = ins::schemasHeap.CreateSchema(this, size, traverser);
   this->id = ins::schemasHeap.GetSchemaID(desc);
}

size_t ManagedSchema::size() {
   return ins::schemasHeap.GetSchemaDesc(this->id)->base_size;
}

struct MyClass : ins::ManagedClass<MyClass> {
   MyClass* parent = 0;
   MyClass* next = 0;
   std::string name = "hello";
   static void __traverser__(ins::TraversalContext<SchemaDesc, MyClass>& context) {
      context.visit_ref(context, offsetof(MyClass, parent));
      context.visit_ref(context, offsetof(MyClass, next));
   }
   static ManagedClass<MyClass> schema;
};

ManagedSchema ManagedClass<MyClass>::schema;

void test_schemas() {
   auto p = new MyClass();
   ins::ObjectInfos infos(p);
   auto trace = infos.getAnalyticsInfos();
}