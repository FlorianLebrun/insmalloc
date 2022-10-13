#pragma once
#include <ins/memory/space.h>

namespace ins {

   struct SchemaDesc;
   struct ISchemaInfos;
   typedef uint32_t SchemaID;

   template<typename tSchema = SchemaDesc, typename tData = void>
   struct TraversalContext {
      typedef void (*fRefVisitor)(TraversalContext& self, uint32_t offset);
      tSchema* schema;
      tData* data;
      fRefVisitor visit_ref;
   };

   typedef void (*ObjectTraverser)(TraversalContext<>& context);

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
         auto refs = (Ref*)&ObjectBytes(schema)[sizeof(SchemaFieldMap)];
         for (int i = 0; schema->refs_count; i++) {
            context.visit_ref(context, refs[i].offset);
         }
      }
   };

   struct SchemasHeap {

      struct SchemaArena : ArenaDescriptor {
         SchemaArena() : ArenaDescriptor(0, cst::ArenaSizeL2) {
            this->availables_count--;
            _ASSERT(this->availables_count == 0);
         }
      };

      std::mutex lock;
      SchemaArena arena;

      uintptr_t alloc_region_size;
      uintptr_t alloc_cursor;
      uintptr_t alloc_commited;
      uintptr_t alloc_end;

      SchemasHeap();
      SchemaDesc* CreateSchema(ISchemaInfos* infos, uint32_t base_size, ObjectTraverser traverser);
      SchemaFieldMap* CreateFieldMapSchema(uint32_t refs_count);
      void DeleteSchema(SchemaDesc* desc);

      SchemaID GetSchemaID(SchemaDesc* schema) { return (uintptr_t(schema) - this->arena.base) / sizeof(SchemaDesc); }
      SchemaDesc* GetSchemaDesc(SchemaID id) { return (SchemaDesc*)((uintptr_t(id) * sizeof(SchemaDesc)) + this->arena.base); }
   };

   extern SchemasHeap schemasHeap;
}
