#include <ins/memory/heap.h>
#include <ins/memory/schemas.h>
#include <ins/timing.h>

namespace ins {
   MemoryHeap* heap = 0;
   _declspec(thread) MemoryContext* context = 0;
}

using namespace ins;

MemoryContextOwner::MemoryContextOwner(MemoryContext* context) {
   if (ins::context) throw "previous context shall be released";
   if (!context->owning.try_lock()) throw "context already owned";
   ins::context = context;
}

MemoryContextOwner::~MemoryContextOwner() {
   ins::context->owning.unlock();
}

static MemoryHeap& ins_get_default_heap_slow() {
   static std::mutex lock;
   std::lock_guard<std::mutex> guard(lock);
   if (!ins::heap) {
      ins::heap = Descriptor::New<MemoryHeap>();
   }
   return *ins::heap;
}

MemoryHeap& ins_get_heap() {
   if (ins::heap) return *ins::heap;
   else return ins_get_default_heap_slow();
}

MemoryContext* ins_get_thread_context() {
   return ins::context;
}

void* ins_malloc(size_t size) {
   if (auto context = ins::context) return context->AllocateBuffer(size);
   else return ins_get_heap().defaultContext->AllocateBuffer(size);
}

void ins_free(void* ptr) {
   if (auto context = ins::context) return context->FreeBuffer(ptr);
   else return ins_get_heap().defaultContext->FreeBuffer(ptr);
}

ObjectHeader ins_malloc_ex(size_t size) {
   if (auto context = ins::context) return context->AllocateObject(size);
   else return ins_get_heap().defaultContext->AllocateObject(size);
}

ins::ObjectHeader ins_malloc_schema(ins::SchemaID schemaID) {
   ObjectHeader obj;
   auto desc = ins::schemasHeap.GetSchemaDesc(schemaID);
   if (auto context = ins::context) obj = context->AllocateObject(desc->base_size);
   else obj = ins_get_heap().defaultContext->AllocateObject(desc->base_size);
   obj->schemaID = schemaID;
   return obj;
}

void ins_free_ex(ObjectHeader obj) {
   if (auto context = ins::context) return context->FreeObject(obj);
   else return ins_get_heap().defaultContext->FreeObject(obj);
}

void* ins_calloc(size_t count, size_t size) {
   return malloc(count * size);
}

bool ins_check_overflow(void* ptr) {
   ObjectInfos infos(ptr);
   return infos.detectOverflowedBytes() == 0;
}

bool ins_get_metadata(void* ptr, ins::ObjectAnalyticsInfos& meta) {
   ObjectInfos infos(ptr);
   if (auto pmeta = infos.getAnalyticsInfos()) {
      meta = *pmeta;
      return true;
   }
   return false;
}

size_t ins_msize(void* ptr, tp_ins_msize default_msize) {
   ObjectInfos infos(ptr);
   if (infos.object) {
      return infos.usable_size();
   }
   else if (default_msize) {
      return default_msize(ptr);
   }
   return 0;
}

void* sat_realloc(void* ptr, size_t size, tp_ins_realloc default_realloc) {
   ObjectInfos infos(ptr);
   if (infos.object) {
      if (size == 0) {
         ins_free(ptr);
         return 0;
      }
      else if (size > infos.usable_size()) {
         void* new_ptr = ins_malloc(size);
         memcpy(new_ptr, ptr, infos.usable_size());
         ins_free(ptr);
         return new_ptr;
      }
      else {
         return ptr;
      }
   }
   else {
      if (default_realloc) {
         return default_realloc(ptr, size);
      }
      else {
         void* new_ptr = ins_malloc(size);
         __try { memcpy(new_ptr, ptr, size); }
         __except (1) {}
         printf("sat cannot realloc unkown buffer\n");
         return new_ptr;
      }
   }
   return 0;
}

/**********************************************************************
*
*   MemoryHeap
*
***********************************************************************/

MemoryHeap::MemoryHeap() {
   for (int i = 1; i < 32; i++) {
      this->objects_slabbed[i].Initiate(i);
   }
   this->defaultContext = Descriptor::New<MemorySharedContext>();
   this->defaultContext->allocated = true;
   this->defaultContext->Initiate(this);
}

MemoryHeap::~MemoryHeap() {
   {
      std::lock_guard<std::mutex> guard(this->contexts_lock);
      while (this->contexts) {
         auto context = this->contexts;
         this->contexts = context->next;
         if (context->allocated) {
            throw std::exception("Cannot delete heap with alive contexts");
         }
         context->Clean();
         context->Dispose();
      }
   }
   this->Clean();
}

size_t MemoryHeap::GetSize() {
   return sizeof(*this);
}

void MemoryHeap::Clean() {
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   for (auto context = this->contexts; context; context = context->next) {
      context->Clean();
   }
   for (int i = 1; i < 32; i++) {
      this->objects_slabbed[i].Clean();
   }
}

void MemoryHeap::CheckValidity() {
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   for (auto context = this->contexts; context; context = context->next) {
      context->CheckValidity();
   }
   for (int i = 1; i < 32; i++) {
      this->objects_slabbed[i].CheckValidity();
   }
}

MemoryContext* MemoryHeap::AcquireContext() {
   {
      std::lock_guard<std::mutex> guard(this->contexts_lock);
      for (auto context = this->contexts; context; context = context->next) {
         if (!context->allocated) {
            context->allocated = true;
            return context;
         }
      }
   }
   {
      auto context = Descriptor::New<MemoryContext>();
      context->allocated = true;
      context->Initiate(this);
      return context;
   }
}

void MemoryHeap::DisposeContext(MemoryContext* _context) {
   auto context = static_cast<MemoryContext*>(_context);
   if (context->heap == this && context->allocated) {
      context->allocated = false;
   }
}

/**********************************************************************
*
*   MemoryContext
*
***********************************************************************/

size_t MemoryContext::GetSize() {
   return sizeof(*this);
}

void MemoryHeap::SetTimeStampOption(bool enabled) {
   std::lock_guard<std::mutex> guard(heap->contexts_lock);
   MemoryAllocOptions options;
   options.enableTimeStamp = 1;
   if (enabled) {
      for (auto ctx = this->contexts; ctx; ctx = ctx->next)
         ctx->options.enableds |= enabled;
   }
   else {
      for (auto ctx = this->contexts; ctx; ctx = ctx->next)
         ctx->options.enableds &= ~enabled;
   }
}

void MemoryHeap::SetStackStampOption(bool enabled) {
   std::lock_guard<std::mutex> guard(heap->contexts_lock);
   MemoryAllocOptions options;
   options.enableStackStamp = 1;
   if (enabled) {
      for (auto ctx = this->contexts; ctx; ctx = ctx->next)
         ctx->options.enableds |= enabled;
   }
   else {
      for (auto ctx = this->contexts; ctx; ctx = ctx->next)
         ctx->options.enableds &= ~enabled;
   }
}

void MemoryHeap::SetSecurityPaddingOption(uint32_t paddingSize) {
   std::lock_guard<std::mutex> guard(heap->contexts_lock);
   for (auto context = this->contexts; context; context = context->next) {
      context->options.enableSecurityPadding = paddingSize;
   }
}

void MemoryContext::Initiate(MemoryHeap* heap) {
   this->heap = heap;
   for (int i = 1; i < 32; i++) {
      this->objects_slabbed[i].Initiate(&heap->objects_slabbed[i]);
   }
   std::lock_guard<std::mutex> guard(heap->contexts_lock);
   this->next = heap->contexts;
   heap->contexts = this;
}

void MemoryContext::Clean() {
   for (int i = 1; i < 32; i++) {
      this->objects_slabbed[i].Clean();
   }
}

void MemoryContext::CheckValidity() {
   for (int i = 1; i < 32; i++) {
      this->objects_slabbed[i].CheckValidity();
   }
}

ObjectHeader MemoryContext::AllocateObject(size_t size) {
   if (!options.enableds) {
      auto layout = getLayoutForSize(size);
      auto infos = ins::ObjectLayoutInfos[layout];
      _ASSERT(size <= infos.object_size);
      if (infos.policy == SlabbedObjectPolicy) {
         return this->objects_slabbed[layout].AllocatePrivateObject();
      }
      else if (infos.policy == LargeObjectPolicy) {
         return this->objects_large[layout].AllocatePrivateObject(size);
      }
      else {
         return this->objects_uncached.AllocatePrivateObject(size);
      }
   }
   else {
      return this->AllocateInstrumentedObject(size);
   }
}

ObjectHeader MemoryContext::AllocateInstrumentedObject(size_t size) {
   size_t requiredSize = size + this->options.enableSecurityPadding;
   if (this->options.enableAnalytics) requiredSize += sizeof(ObjectAnalyticsInfos);
   auto layout = getLayoutForSize(requiredSize);
   auto infos = ins::ObjectLayoutInfos[layout];

   // Allocate memory
   ObjectHeader obj = 0;
   if (infos.policy == SlabbedObjectPolicy) {
      obj = this->objects_slabbed[layout].AllocatePrivateObject();
   }
   else if (infos.policy == LargeObjectPolicy) {
      obj = this->objects_large[layout].AllocatePrivateObject(requiredSize);
   }

   // Configure analytics infos
   uint32_t bufferLen = ins::ObjectLayoutInfos[layout].object_size;
   if (this->options.enableAnalytics) {
      bufferLen -= sizeof(ObjectAnalyticsInfos);
      auto infos = (ObjectAnalyticsInfos*)&ObjectBytes(obj)[bufferLen];
      infos->timestamp = ins::timing::getCurrentTimestamp();
      infos->stackstamp = 42;
      obj->hasAnalyticsInfos = 1;
   }

   // Configure security padding
   if (this->options.enableSecurityPadding) {
      uint8_t* paddingBytes = &ObjectBytes(obj)[size];
      uint32_t paddingLen = bufferLen - size - sizeof(uint32_t);
      uint32_t& paddingTag = (uint32_t&)paddingBytes[paddingLen];
      paddingTag = size ^ 0xabababab;
      memset(paddingBytes, 0xab, paddingLen);
      obj->hasSecurityPadding = 1;
   }

   return obj;
}

void MemoryContext::FreeObject(ObjectHeader obj) {
   address_t address(obj);
   auto arena = MemorySpace::state.table[address.arenaID];
   auto regionID = address.position >> arena.segmentation;
   auto entry = arena.descriptor()->regions[regionID];
   if (entry.IsObjectRegion()) {
      auto objectLayoutID = entry.objectLayoutID;
      if (objectLayoutID <= SlabbedObjectLayoutMax) {
         auto region = SlabbedObjectRegion(regionID << arena.segmentation);
         this->objects_slabbed[objectLayoutID - SlabbedObjectLayoutMin].FreeObject(obj, region);
      }
      else if (objectLayoutID <= LargeObjectLayoutMax) {
         auto region = LargeObjectRegion(regionID << arena.segmentation);
         this->objects_large[objectLayoutID - LargeObjectLayoutMin].FreeObject(region);
      }
      else {
         auto region = LargeObjectRegion(regionID << arena.segmentation);
         this->objects_uncached.NotifyAvailableRegion(region);
      }
   }
}

address_t MemoryContext::AllocateBuffer(size_t size) {
   size += sizeof(ObjectHeader);
   return &this->AllocateObject(size)[1];
}

void MemoryContext::FreeBuffer(address_t address) {
   ObjectLocation loc(address);
   if (loc.object) {
      auto objectLayoutID = loc.entry.objectLayoutID;
      if (objectLayoutID <= SlabbedObjectLayoutMax) {
         auto region = SlabbedObjectRegion(loc.region);
         this->objects_slabbed[objectLayoutID - SlabbedObjectLayoutMin].FreeObject(loc.object, region);
      }
      else if (objectLayoutID <= LargeObjectLayoutMax) {
         auto region = LargeObjectRegion(loc.region);
         this->objects_large[objectLayoutID - LargeObjectLayoutMin].FreeObject(region);
      }
      else {
         auto region = LargeObjectRegion(loc.region);
         this->objects_uncached.NotifyAvailableRegion(region);
      }
   }
}
