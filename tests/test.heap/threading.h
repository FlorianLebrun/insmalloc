#pragma once
#include <ins/memory/objects-base.h>
#include <ins/memory/controller.h>

namespace ins::mem {

   struct ThreadMemoryContext {
      bool disposable;
      ThreadMemoryContext(MemoryContext* context = 0, bool disposable = false);
      ~ThreadMemoryContext();
      MemoryContext& operator *();
      MemoryContext* operator ->();
      void Put(MemoryContext* context = 0, bool disposable = false);
      MemoryContext* Pop();
   };

   struct ThreadStackTracker : IObjectReferenceTracker {
      static __declspec(thread) ThreadStackTracker* current;

      struct Local {
      private:
         Local** pprev;
         Local* next;
         friend class ThreadStackTracker;
      public:
         void* ptr;
         Local(void* ptr) : ptr(ptr) {
            _ASSERT(current);
            this->pprev = &current->locals;
            this->next = current->locals;
            current->locals = this;
         }
         ~Local() {
            this->pprev[0] = this->next;
         }
      };

      ThreadStackTracker* parent = 0;
      Local* locals = 0;

      ThreadStackTracker() {
         mem::RegisterReferenceTracker(this);
         this->parent = current;
         current = this;
      }
      ~ThreadStackTracker() {
         mem::UnregisterReferenceTracker(this);
         current = this->parent;
      }
      void MarkObjects(ObjectAnalysisSession& session) override;
   };

   // Collected Local: explicit local stack reference to a collected object
   template <typename T>
   struct Local {
   private:
      ThreadStackTracker::Local site;
   public:
      Local(T* object = 0)
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

/*
   // Weak Reference: reference a disposable object
   template<typename T>
   struct WRef {
   private:
      mem::ObjectHeader header = 0;
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
            this->header = mem::ObjectHeader(nwptr)[-1];
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
   struct LRef {
   private:
      mem::ObjectHeader header = 0;
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
            this->header = mem::ObjectHeader(nwptr)[-1];
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

*/
}

