#include <stdint.h>
#include <stdlib.h>
#include "./descriptors.h"
#include "./structs.h"

namespace ins {

   typedef struct sObjectHeader {
      uint64_t hasAnaliticsTrace : 1; // define that object is followed by analytics structure
      uint64_t hasSecurityPadding : 1; // define that object is followed by a padding of security(canary)
      uint64_t retentionCounter : 8; // define how many times the object shall be dispose to be really deleted
      uint64_t weakRetentionCounter : 6; // define how many times the object shall be unlocked before the memory is reusable
      uint64_t lock : 1; // general multithread protection for this object
   } *ObjectHeader;

   typedef struct sObjectChain : sObjectHeader {
      sObjectChain* nextObject;
      sObjectChain* nextList;
   } *ObjectChain;

   typedef struct sObjectSlab {
      uint32_t uses; // Bitmap of allocated entries
      uint32_t usables; // Bitmap of allocable entries
      uint32_t gc_marks; // Bitmap of gc marked entries
      uint32_t gc_analyzis; // Bitmap of gc analyzed entries
      uint32_t slab_index; // Absolute slab index
      uint8_t class_id;
      uint8_t __reserve[11];
   } *ObjectSlab;
   static_assert(sizeof(sObjectSlab) == 32, "bad size");

   struct sObjectBucket {
      ObjectSlab firstSlab = 0;
      ObjectChain firstObject = 0;
      ObjectSlab AcquireSlab() {
         return 0;
      }
   };

   typedef struct sObjectRegion : sDescriptor {
      uint8_t classid = 0;
      size_target_t sizing;

      uint16_t slab_rangeL2 = 0;
      uint16_t slab_max = 0;
      uint16_t slab_count = 0;

      sObjectBucket bucket;

      sObjectRegion(size_target_t sizing) : sizing(sizing) {
      }
      size_t GetSize() override {
         return sizeof(*this) + this->slab_max * sizeof(sObjectSlab);
      }
      index_t GetObjectIndex(address_t addr) {
         sizing.divide(addr.position);
         return 0;
      }
      ObjectHeader AllocateObject() {
         return 0;
      }
      ObjectSlab AcquireSlab() {
         for (int i = 0; i < 4; i++) {
            if (auto slab = this->bucket.AcquireSlab()) {
               return slab;
            }
         }
         if (this->ExtendActiveZone()) {
            return this->AcquireSlab();
         }
         return 0;
      }
      bool ExtendActiveZone() {
         //this->slab_countL2++;
         return false;
      }
      ObjectSlab ReleaseObjects(ObjectChain objs) {
      }
      ObjectSlab get(uint32_t slabIndex) {
         return &ObjectSlab(&this[1])[slabIndex];
      }
   } *ObjectRegion;


}
