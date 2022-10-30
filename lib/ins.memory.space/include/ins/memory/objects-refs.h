
#include <ins/memory/objects-base.h>

namespace ins {

   /**********************************************************************
   *
   *   Object References
   *
   ***********************************************************************/
   struct MemoryLocalSite;

   // Weak Reference: reference a disposable object
   template<typename T>
   struct WeakRef {
   private:
      ObjectHeader header = 0;
   public:
      bool alive() {
         return true;
      }
      T* get() {
         if (this->header) {
            return &this->header[1];
         }
         return 0;
      }
      void set(T* nwptr) {
         if (auto prev = this->header) {
            this->header = 0;
            prev->ReleaseWeak();
         }
         if (nwptr) {
            this->header = ObjectHeader(nwptr)[-1];
            this->header->RetainWeak();
         }
      }
      T* operator -> () {
         return this->get();
      }
      T* operator = (T* nwptr) {
         this->set(nwptr);
      }
   };

   // Lock Reference: reference an object and guarantee object aliveness
   template<typename T>
   struct LockRef {
   private:
      ObjectHeader header = 0;
   public:
      T* get() {
         if (this->header) {
            return &this->header[1];
         }
         return 0;
      }
      void set(T* nwptr) {
         if (auto prev = this->header) {
            this->header = 0;
            prev->Release();
         }
         if (nwptr) {
            this->header = ObjectHeader(nwptr)[-1];
            this->header->Retain();
         }
      }
      T* operator -> () {
         return this->get();
      }
      T* operator = (T* nwptr) {
         this->set(nwptr);
      }
   };

   // Collected Reference: reference a collected object from a collected object
   template<typename T>
   struct CRef {
   private:
      T* ptr = 0;
   public:
      T* operator -> () {
         return this->ptr;
      }
      T* operator = (T* nwptr) {
         if (auto session = ObjectAnalysisSession::enabled) {
            session->MarkPtr(nwptr);
         }
         return this->ptr = nwptr;
      }
   };

   // Collected Local: explicit local stack reference to a collected object
   template <typename T>
   struct CLocal {
   private:
      MemoryLocalSite site;
   public:
      CLocal(T* object = 0)
         : site(object) {
      }
      T* get() {
         return (T*)this->site.ptr;
      }
      operator T* () {
         return this->get();
      }
      T* operator -> () {
         return this->get();
      }
      T* operator = (T* object) {
         this->site.ptr = object;
         return object;
      }
      T* release() {
         auto object = this->get();
         this->site.ptr = 0;
         return object;
      }
   };
}
