#include "./threading.h"
#include <ins/memory/contexts.h>

using namespace ins;
using namespace ins::mem;

__declspec(thread) mem::ThreadStackTracker* mem::ThreadStackTracker::current = 0;

void mem::ThreadStackTracker::MarkObjects(ObjectAnalysisSession& session) {
   for (auto loc = this->locals; loc; loc = loc->next) {
      session.MarkPtr(loc->ptr);
   }
}

mem::ThreadMemoryContext::ThreadMemoryContext(MemoryContext* context, bool disposable) {
   this->Put(context, disposable);
}

mem::ThreadMemoryContext::~ThreadMemoryContext() {
   bool disposable = this->disposable;
   auto context = this->Pop();
   if (context && disposable) {
      mem::DisposeContext(context);
   }
}

mem::MemoryContext& mem::ThreadMemoryContext::operator *() {
   return *mem::CurrentContext;
}

mem::MemoryContext* mem::ThreadMemoryContext::operator ->() {
   return mem::CurrentContext;
}

void mem::ThreadMemoryContext::Put(MemoryContext* context, bool disposable) {
   if (mem::CurrentContext) {
      throw "previous context shall be released";
   }
   if (!context) {
      context = mem::AcquireContext(false);
      this->disposable = true;
   }
   else if (context->isShared) {
      throw "cannot use shared context";
   }
   else {
      this->disposable = disposable;
   }
   if (!context->owning.try_lock()) {
      throw "context already owned";
   }
   context->thread = os::Thread::current();
   mem::CurrentContext = context;
}

mem::MemoryContext* mem::ThreadMemoryContext::Pop() {
   auto context = mem::CurrentContext;
   if (context) {
      context->thread.Clear();
      context->owning.unlock();
      mem::CurrentContext = 0;
   }
   return context;
}
