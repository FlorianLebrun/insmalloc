#pragma once
#include <ins/memory/map.h>
#include <ins/memory/objects-base.h>

namespace ins::mem {

   typedef uint32_t ObjectSchemaID;

   struct IObjectSchema {
      virtual const char* GetSchemaName() = 0;
   };

   template<typename sSchema = sObjectSchema, typename sData = void>
   struct TraversalContext {
      typedef void (*fVisitPtr)(TraversalContext* self, void* ptr);
      sSchema* schema;
      sData* data;
      fVisitPtr visit_ptr;
      void visit_ref(uint32_t offset) {
         this->visit_ptr(this, (void*&)ObjectBytes(this->data)[offset]);
      }
   };

   typedef void (*ObjectTraverser)(TraversalContext<>* context);
   typedef void (*ObjectFinalizer)(void* ptr);

   struct sObjectSchema {

      enum StandardSchemaID {
         OpaqueID = 0,
         InvalidateID = 1,
      };

      uint8_t type_id = 0;
      uint32_t base_size = 0;
      IObjectSchema* infos = 0;
      ObjectTraverser traverser = 0;
      ObjectFinalizer finalizer = 0;
   };
   static_assert(sizeof(sObjectSchema) == 32, "bad size");

   struct ManagedSchema : IObjectSchema {
      ObjectSchemaID id = 0;
      const char* type_name = 0;
      const char* GetSchemaName() override { return this->type_name; }
      void InstallSchema(const std::type_info& info, size_t size, ObjectTraverser traverser, ObjectFinalizer finalizer);
      size_t size();
   };

   struct ManagedClassBase {

      // Collected Reference: reference a collected object from a collected object
      template<typename T>
      struct Ref {
      private:
         T* ptr = 0;
      public:
         T* operator -> () {
            return this->ptr;
         }
         T* operator = (T* nwptr) {
            if (auto session = mem::ObjectAnalysisSession::enabled) {
               session->MarkPtr(nwptr);
            }
            return this->ptr = nwptr;
         }
      };
   };

   template<class T>
   struct ManagedClass : ManagedClassBase {
      static ManagedSchema schema;
      typedef typename T Class;
      static void __finalizer__(T* ptr) {
         ptr->~Class();
      }
      void* operator new(size_t size) {
         if (schema.id == 0) {
            schema.InstallSchema(typeid(T), sizeof(T), ObjectTraverser(&T::__traverser__), ObjectFinalizer(&__finalizer__));
            _ASSERT(schema.size() == size);
         }
         return AllocateManagedObject(schema.id);
      }
      void operator delete(void* ptr) {
         FreeObject(ptr);
      }
   };

   extern sObjectSchema* ObjectSchemas;
   inline ObjectSchemaID GetObjectSchemaID(ObjectSchema schema) { return (uintptr_t(schema) - uintptr_t(ObjectSchemas)) / sizeof(sObjectSchema); }
   inline ObjectSchema GetObjectSchema(ObjectSchemaID id) { return &ObjectSchemas[id]; }
   extern ObjectSchema CreateObjectSchema(IObjectSchema* infos, uint32_t base_size, ObjectTraverser traverser, ObjectFinalizer finalizer);

}
