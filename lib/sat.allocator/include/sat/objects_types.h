#pragma once
#include <vector>

namespace sat {
   typedef uint32_t TypeDefID;
   typedef struct tTypeDef* TypeDef;
   typedef struct ITypeHandle* TypeHandle;
   struct ITypesController;

   struct IReferenceVisitor {
      virtual void visit(void* ref) = 0;
   };

   struct ITypeHandle {
      virtual void getTitle(char* buffer, int size) = 0;
      virtual void traverse(sat::IReferenceVisitor* visitor) = 0;
   };
   
   struct tTypeBuffer {
      uint32_t length; // Size of this structure in memory
   };

#pragma warning(disable : 4200)
   struct tTypeDef : tTypeBuffer {
      TypeHandle handle;
      uint32_t nrefs;
      uint32_t refs[0]; // list of refs offset
   };

   struct TypesController {
      virtual void* alloc(int size) = 0;
      virtual void free(void* ptr) = 0;

      virtual TypeDef getType(TypeDefID typeID) = 0;
      virtual TypeDefID getTypeID(TypeDef typd) = 0;

      virtual TypeDef allocTypeDef(ITypeHandle* handle, uint32_t nrefs) = 0;
   };
}

extern"C" SAT_API uintptr_t sat_types_base;
extern"C" SAT_API sat::TypesController * sat_get_types_controller();
