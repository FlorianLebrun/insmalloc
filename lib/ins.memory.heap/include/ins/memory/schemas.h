#pragma once
#include <ins/memory/regions.h>
#include <ins/memory/objects-base.h>

namespace ins::mem {

   struct SchemaDesc;
   struct ISchemaInfos;
   typedef uint32_t SchemaID;

   template<typename tSchema = SchemaDesc, typename tData = void>
   struct TraversalContext {
      typedef void (*fVisitPtr)(TraversalContext* self, void* ptr);
      tSchema* schema;
      tData* data;
      fVisitPtr visit_ptr;
      void visit_ref(uint32_t offset) {
         this->visit_ptr(this, (void*&)ObjectBytes(this->data)[offset]);
      }
   };

   typedef void (*ObjectTraverser)(TraversalContext<>* context);

   struct ISchemaInfos {
      virtual const char* name() = 0;
   };

   struct SchemaDesc {
      uint32_t base_size;
      ISchemaInfos* infos = 0;
      ObjectTraverser traverser = 0;
      uint64_t _unused;
   };
   static_assert(sizeof(SchemaDesc) == 32, "bad size");

   struct SchemaFieldMap : SchemaDesc, ISchemaInfos {
      struct Ref {
         uint32_t offset;
      };
      uint32_t refs_count;
      static void __traverser__(TraversalContext<SchemaFieldMap>& context) {
         auto schema = context.schema;
         auto refs = (Ref*)&BufferBytes(schema)[sizeof(SchemaFieldMap)];
         for (int i = 0; schema->refs_count; i++) {
            context.visit_ref(refs[i].offset);
         }
      }
   };

   struct SchemasHeap : RegionsSpaceInitiator {

      struct SchemaArena : ArenaDescriptor {
         SchemaArena() {
            this->Initiate(cst::ArenaSizeL2);
            this->availables_count--;
            _ASSERT(this->availables_count == 0);
         }
      };

      uintptr_t arenaBase;
      SchemaArena arena;

      std::mutex lock;
      uintptr_t alloc_region_size;
      uintptr_t alloc_cursor;
      uintptr_t alloc_commited;
      uintptr_t alloc_end;

      SchemasHeap();
      SchemaDesc* CreateSchema(ISchemaInfos* infos, uint32_t base_size, ObjectTraverser traverser);

      SchemaID GetSchemaID(SchemaDesc* schema) { return (uintptr_t(schema) - this->arenaBase) / sizeof(SchemaDesc); }
      SchemaDesc* GetSchemaDesc(SchemaID id) { return (SchemaDesc*)((uintptr_t(id) * sizeof(SchemaDesc)) + this->arenaBase); }
   };

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
         return ins_new_managed(schema.id)->ptr();
      }
      void operator delete(void* ptr) {
         ins_free(ptr);
      }
   };

   extern SchemasHeap schemasHeap;
}
