#include <ins/memory/objects-context.h>
#include <ins/memory/space.h>

using namespace ins;

/**********************************************************************
*
*   ObjectRegionPool
*
***********************************************************************/

void ObjectRegionPool::CheckValidity() {
   uint32_t c_known_empty_count = 0;

   this->ScavengeNotifiedRegions();

   this->usables.CheckValidity();
   for (ObjectRegion x = this->usables.first; x; x = x->next.used) {
      if (x->IsDisposable()) c_known_empty_count++;
   }

   this->disposables.CheckValidity();
   for (ObjectRegion x = this->disposables.first; x; x = x->next.used) {
      if (x->IsDisposable()) c_known_empty_count++;
   }

   _ASSERT(this->owneds_count >= this->usables.count + this->disposables.count);

   uint32_t c_regions_count = 0;
   uint32_t c_regions_empty_count = 0;
   MemorySpace::ForeachObjectRegion(
      [&](ObjectRegion region) {
         intptr_t offset = intptr_t(this) - intptr_t(region->owner);
         if (offset == offsetof(ObjectClassProvider, regions_pool)) {
            if (region->IsDisposable()) {
               c_regions_empty_count++;
            }
            c_regions_count++;
         }
         return true;
      }
   );
   _ASSERT(c_regions_count == this->owneds_count);
   _ASSERT(c_regions_empty_count == c_known_empty_count);
}
