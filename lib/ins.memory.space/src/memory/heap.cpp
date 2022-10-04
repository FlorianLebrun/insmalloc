#include <ins/memory/heap.h>

using namespace ins;

MemoryHeap::MemoryHeap() {
   for (int i = 1; i < 32; i++) {
      this->objects[i].Initiate(i);
   }
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

void MemoryHeap::Clean() {
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   for (auto context = this->contexts; context; context = context->next) {
      context->Clean();
   }
   for (int i = 1; i < 32; i++) {
      this->objects[i].Clean();
   }
}

void MemoryHeap::CheckValidity() {
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   for (auto context = this->contexts; context; context = context->next) {
      context->CheckValidity();
   }
   for (int i = 1; i < 32; i++) {
      this->objects[i].CheckValidity();
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
   return MemorySpace::state.descriptorHeap->New<sContext>(this);
}

void MemoryHeap::DisposeContext(MemoryContext* _context) {
   auto context = static_cast<MemoryHeap::sContext*>(_context);
   if (context->heap == this && context->allocated) {
      context->allocated = false;
   }
}

MemoryHeap::sContext::sContext(MemoryHeap* heap) : MemoryContext(heap) {
   std::lock_guard<std::mutex> guard(heap->contexts_lock);
   this->next = heap->contexts;
   heap->contexts = this;
}

size_t MemoryHeap::sContext::GetSize() {
   return sizeof(*this);
}

MemoryContext::MemoryContext(MemoryHeap* heap) : heap(heap) {
   for (int i = 1; i < 32; i++) {
      this->objects[i].Initiate(&heap->objects[i]);
   }
}

void MemoryContext::Clean() {
   for (int i = 1; i < 32; i++) {
      this->objects[i].Clean();
   }
}

void MemoryContext::CheckValidity() {
   for (int i = 1; i < 32; i++) {
      this->objects[i].CheckValidity();
   }
}
