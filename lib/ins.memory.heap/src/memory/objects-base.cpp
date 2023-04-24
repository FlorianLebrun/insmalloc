#include <stdint.h>
#include <semaphore>
#include <stdlib.h>
#include <ins/memory/objects-base.h>
#include <ins/memory/space.h>

using namespace ins;
using namespace ins::mem;

std::mutex mem::ObjectAnalysisSession::running;
ObjectAnalysisSession* mem::ObjectAnalysisSession::enabled = 0;

/**********************************************************************
*
*   Object Region
*
***********************************************************************/

sObjectRegion* sObjectRegion::New(bool managed, uint8_t layoutID, ObjectLocalContext* owner) {
   auto& infos = cst::ObjectLayoutInfos[layoutID];

   auto ptr = managed
      ? Regions.AllocateManagedRegion(infos.region_sizeL2, infos.region_sizingID, owner->context)
      : Regions.AllocateUnmanagedRegion(infos.region_sizeL2, infos.region_sizingID, owner->context);

   auto region = new(ptr) sObjectRegion(layoutID, size_t(1) << infos.region_sizeL2, owner);
   RegionLocation::New(region).layout() = layoutID;
   return region;
}

sObjectRegion* sObjectRegion::New(bool managed, uint8_t layoutID, size_t size, ObjectLocalContext* owner) {

   void* ptr = managed
      ? mem::Regions.AllocateManagedRegionEx(size + sizeof(sObjectRegion), owner->context)
      : mem::Regions.AllocateUnmanagedRegionEx(size + sizeof(sObjectRegion), owner->context);

   auto region = new(ptr) sObjectRegion(layoutID, size, owner);
   RegionLocation::New(ptr).layout() = layoutID;
   return region;
}

void sObjectRegion::DisplayToConsole() {
   auto& infos = cst::ObjectLayoutInfos[this->layoutID];

   address_t addr(this);
   auto region_size = size_t(1) << infos.region_sizeL2;
   printf("\n%X%.8llX %s:", int(addr.arenaID), int64_t(addr.position), sz2a(region_size).c_str());

   auto nobj = this->GetAvailablesCount();
   auto nobj_max = infos.region_objects;
   printf(" layout(%d) objects(%d/%d)", this->layoutID, int(nobj), int(nobj_max));
   printf(" owner(%p)", this->owner);
   if (this->IsDisposable()) printf(" [empty]");
}

void sObjectRegion::Dispose() {
   auto& infos = cst::ObjectLayoutInfos[this->layoutID];
   mem::Regions.DisposeRegion(this, infos.region_sizeL2, infos.region_sizingID);
}
