#include <stdint.h>
#include <semaphore>
#include <stdlib.h>
#include <ins/memory/objects-base.h>
#include <ins/memory/space.h>

using namespace ins;

std::mutex ins::ObjectAnalysisSession::running;
ObjectAnalysisSession* ins::ObjectAnalysisSession::enabled = 0;

/**********************************************************************
*
*   Object Region
*
***********************************************************************/

sObjectRegion* sObjectRegion::New(bool managed, uint8_t layoutID, ObjectLocalContext* owner) {
   auto& infos = cst::ObjectLayoutInfos[layoutID];

   auto list = managed ? ins::RegionsHeap.arenas_managed : ins::RegionsHeap.arenas_unmanaged;
   auto ptr = list[infos.region_sizeL2].AllocateRegion(infos.region_sizingID, owner->context);

   auto region = new(ptr) sObjectRegion(layoutID, size_t(1) << infos.region_sizeL2, owner);
   RegionLocation::New(region).layout() = layoutID;
   return region;
}

sObjectRegion* sObjectRegion::New(bool managed, uint8_t layoutID, size_t size, ObjectLocalContext* owner) {

   void* ptr = managed
      ? ins::RegionsHeap.AllocateManagedRegionEx(size + sizeof(sObjectRegion), owner->context)
      : ins::RegionsHeap.AllocateUnmanagedRegionEx(size + sizeof(sObjectRegion), owner->context);

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
   ins::RegionsHeap.DisposeRegion(this, infos.region_sizeL2, infos.region_sizingID);
}
