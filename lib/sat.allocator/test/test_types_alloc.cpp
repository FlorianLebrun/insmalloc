#include <sat/allocator.h>
#include "./utils.h"

struct Object {
   virtual void TraverseRefs(sat::IReferenceVisitor* visitor) = 0;
};

struct myClass2 : Object {
   static sat::TypeDefID typeID;
   uint32_t x;
   virtual void TraverseRefs(sat::IReferenceVisitor* visitor) override {}
};
sat::TypeDefID myClass2::typeID = 0;

struct myClass1 : Object {
   static sat::TypeDefID typeID;
   myClass1* x;
   myClass2* y;
   virtual void TraverseRefs(sat::IReferenceVisitor* visitor) override {
      visitor->visit(x);
      visitor->visit(y);
   }
};
sat::TypeDefID myClass1::typeID = 0;

struct ObjectHandler : sat::ITypeHandle {
   std::string name;
   ObjectHandler(std::string name) : name(name) {
   }
   virtual void getTitle(char* buffer, int size) {
      strcpy_s(buffer, size, this->name.c_str());
   }
   virtual void traverse(sat::IReferenceVisitor* visitor) override {

   }
};

void test_types_alloc() {
   {
      sat::TypesController* types = sat_get_types_controller();

      sat::TypeDef def_myClass1 = types->allocTypeDef(new ObjectHandler("myClass1"), 0);
      sat::TypeDef def_myClass2 = types->allocTypeDef(new ObjectHandler("myClass2"), 0);

      myClass1::typeID = types->getTypeID(def_myClass1);
      myClass2::typeID = types->getTypeID(def_myClass2);
   }

   Chrono c;
   static const int count = 5;
   std::vector<myClass1*> objects(count);
   c.Start();

   for (int i = 0; i < count; i++) {
      objects[i] = new (sat_malloc_ex(sizeof(myClass1), myClass1::typeID)) myClass1();
   }
   printf("[sat-malloc] alloc object time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));

   struct Visitor : sat::IObjectVisitor {
      sat::TypesController* types = sat_get_types_controller();
      virtual bool visit(sat::tpObjectInfos obj) override {
         if (auto typ = obj->meta ? types->getType(obj->meta->typeID) : 0) {
            char name[128];
            typ->handle->getTitle(name, sizeof(name));
            printf("%s: %.12llX\n", name, obj->base);
         }
         else printf("object: %.12llX\n", obj->base);
         return true;
      }
   };
   sat_get_contoller()->traverseObjects(&Visitor());

   c.Start();
   for (int i = 0; i < count; i++) {
      int k = fastrand() % objects.size();
      void* obj = objects[k];
      objects[k] = objects.back();
      objects.pop_back();
      sat_free(obj);
   }
   printf("[sat-malloc] free object time = %g ns\n", c.GetDiffFloat(Chrono::NS) / float(count));

}
