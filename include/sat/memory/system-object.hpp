#pragma once
#include <atomic>
#include <stdint.h>
#include <assert.h>

namespace sat {

   // System allocator
   // ------------------------------------------
   namespace memory {

      class sizeID_t {
         uint32_t value;
         sizeID_t(uint32_t value) : value(value) {}
      public:
         static sizeID_t with(uint32_t value) { return sizeID_t(value); }
         operator uint32_t() { return value; }
      };

      SAT_API sizeID_t getSystemSizeID(size_t size);
      SAT_API void* allocSystemBuffer(sizeID_t sizeID);
      SAT_API void freeSystemBuffer(void* ptr, sizeID_t sizeID);

      template <typename T>
      void* allocSystemBuffer() {
         return allocSystemBuffer(getSystemSizeID(sizeof(T)));
      }

      template <class T, class Base>
      class AllocableSystemClass : public Base {
      protected:
         virtual size_t getSize() {
            return sizeof(T);
         }
      public:
         template <typename... Ta> AllocableSystemClass(Ta... t)
            : Base(t...) {
         }
         void* operator new(size_t size) {
            _ASSERT(size == sizeof(T));
            return allocSystemBuffer(getSystemSizeID(sizeof(T)));
         }
         void operator delete(void* ptr) {
            auto* obj = (T*)ptr;
            _ASSERT(obj->getSize() == sizeof(T));
            return freeSystemBuffer(ptr, getSystemSizeID(sizeof(T)));
         }

         template <typename Tc>
         class Derived : public AllocableSystemClass<Tc, T> {
         public:
            template <typename... Ta> Derived(Ta... t)
               : AllocableSystemClass(t...) {
            }
         };
      };

   }

   // System object base
   // ------------------------------------------

   class ISystemObject {
   public:
      virtual void retain() = 0;
      virtual void release() = 0;
      virtual size_t getSize() = 0;
      virtual ~ISystemObject() = default;
   };

   template <class Base = ISystemObject>
   class SystemObject : public memory::AllocableSystemClass<SystemObject<Base>, Base> {
   protected:
      std::atomic_uint32_t numRefs = 1;
   public:
      template <typename... Ta> SystemObject(Ta... t)
         : AllocableSystemClass(t...) {
      }
      virtual void retain() override final {
         assert(this->numRefs >= 0);
         this->numRefs++;
      }
      virtual void release() override final {
         assert(this->numRefs >= 0);
         if (!(--this->numRefs)) this->dispose();
      }
      virtual void dispose() {
         delete this;
      }
   };

}