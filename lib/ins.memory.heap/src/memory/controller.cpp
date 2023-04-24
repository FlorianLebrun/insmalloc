#include <ins/memory/controller.h>
#include <ins/timing.h>
#include <mutex>
#include <thread>
#include <iostream>
#include <condition_variable>

using namespace ins;
using namespace ins::mem;

mem::MemoryController mem::Controller;

namespace ins::mem {
   void __notify_memory_item_init__(uint32_t flag);
}

mem::MemoryController::MemoryController() {
   if (this == &mem::Controller) {
      mem::__notify_memory_item_init__(2);
   }
   else {
      this->Initiate();
   }
}

void mem::MemoryController::Initiate() {
   this->defaultContext = mem::Controller.CreateSharedContext();
   this->worker = std::thread(
      [this]() {
         while (!this->terminating) {
            std::unique_lock<std::mutex> guard(this->notification_lock);
            this->notification_signal.wait(guard);
            if (this->terminating) break;

            auto starved_consumers = this->starved_consumers;
            auto recovered_contexts = this->recovered_contexts;
            this->starved_consumers = 0;
            this->recovered_contexts = 0;
            guard.unlock();

            // Apply memory soft recovery procedures
            while (auto context = recovered_contexts) {
               recovered_contexts = context->next.recovered;
               context->PerformMemoryCleanup();
               context->next.recovered = none<MemoryContext>();
            }

            // Apply memory hard recovery procedures
            if (starved_consumers) {

               // Cleanup heaps
               this->PerformMemoryCleanup();

               // Restart starved consumers
               while (auto consumer = starved_consumers) {
                  starved_consumers = consumer->next;
                  consumer->next = 0;
                  consumer->signal.notify_one();
               }
            }
         }

      }
   );
}

mem::MemoryController::~MemoryController() {

   this->terminating = true;
   this->notification_signal.notify_all();
   this->worker.join();

   std::lock_guard<std::mutex> guard2(this->contexts_lock);
   while (this->contexts) {
      auto context = this->contexts;
      this->contexts = context->next.registered;
      if (context->allocated) {
         context->PerformMemoryCleanup();
         std::cerr << "Delete heap with alive contexts\n";
      }
      Descriptor::Delete(context);
   }
   this->central.PerformMemoryCleanup();

   if (this == &mem::Controller) {
      mem::Regions.PerformMemoryCleanup();
   }
}

void mem::MemoryController::PerformMemoryCleanup() {
   timing::Chrono chrono;

   // Purge allocation caches
   for (auto context = this->contexts; context; context = context->next.registered) {
      context->PerformMemoryCleanup();
   }
   this->central.PerformMemoryCleanup();
   mem::Regions.PerformMemoryCleanup();

   // Sweep unused objects
   this->MarkAndSweepUnusedObjects();

   printf("> cleanup time: %g ms\n", chrono.GetDiffFloat(chrono.MS));
}

void mem::MemoryController::MarkAndSweepUnusedObjects() {
   if (ObjectAnalysisSession::running.try_lock()) {
      _ASSERT(!ObjectAnalysisSession::enabled);
      this->cycle++;

      // Run objects aliveness analysis
      this->MarkUsedObjects();

      // Sweep unused objects
      this->SweepUnusedObjects();

      ObjectAnalysisSession::running.unlock();
   }
}

void mem::MemoryController::MarkUsedObjects() {
   this->cleanup.Reset();
   ObjectAnalysisSession::enabled = &this->cleanup;
   {
      std::lock_guard<std::mutex> guard(this->contexts_lock);
      for (auto ctx = this->contexts; ctx; ctx = ctx->next.registered) {
         ctx->MarkUsedObject();
      }
   }
   this->cleanup.RunOnce();
   ObjectAnalysisSession::enabled = 0;
}

void mem::MemoryController::SweepUnusedObjects() {
   size_t sweptObjects = 0;
   for (size_t i = 0; i < cst::ArenaPerSpace; i++) {
      auto& entry = mem::Regions.ArenaMap[i];
      if (entry.managed) {
         auto arena = entry.descriptor();
         address_t base = i << cst::ArenaSizeL2;

         // Compare aliveness map to the new aliveness snapshot
         auto alivenessSnapshot = &this->cleanup.regionAlivenessMap[this->cleanup.arenaIndexesMap[i]];
         auto startIndex = this->cleanup.arenaIndexesMap[i];
         auto endIndex = this->cleanup.arenaIndexesMap[i + 1];
         auto length = endIndex - startIndex;
         for (size_t i = 0; i < length; i++) {
            if (arena->regions[i].IsObjectRegion()) {
               auto region = ObjectRegion(base.ptr + (i << arena->segmentation));
               auto maskBits = cst::ObjectLayoutMask[region->layoutID];
               auto allocatedBits = (~(region->availables | region->notified_availables)) & maskBits;
               auto unusedBits = allocatedBits & (~alivenessSnapshot[i].flags);
               if (unusedBits) {
                  if (region->notified_availables.fetch_or(unusedBits) == 0) {
                     region->NotifyAvailables(true);
                  }
                  sweptObjects += bit::bitcount_64(unusedBits);
               }
            }
         }
      }
   }

   printf("sweep %lld objects\n", sweptObjects);
}

void mem::MemoryController::CheckValidity() {
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   for (auto context = this->contexts; context; context = context->next.registered) {
      context->CheckValidity();
   }
}

void mem::MemoryController::Print() {
   std::lock_guard<std::mutex> guard(this->contexts_lock);

   struct tContextStats {
      size_t region_count = 0;

      size_t used_bytes = 0;
      size_t notified_bytes = 0;
      size_t avaiblable_bytes = 0;
      size_t total_bytes = 0;

      size_t used_objects = 0;
      size_t notified_objects = 0;
      size_t avaiblable_objects = 0;
      size_t total_objects = 0;

      void add(tContextStats& stat) {
         this->region_count += stat.region_count;

         this->used_bytes += stat.used_bytes;
         this->notified_bytes += stat.notified_bytes;
         this->avaiblable_bytes += stat.avaiblable_bytes;
         this->total_bytes += stat.total_bytes;

         this->used_objects += stat.used_objects;
         this->notified_objects += stat.notified_objects;
         this->avaiblable_objects += stat.avaiblable_objects;
         this->total_objects += stat.total_objects;
      }
      void print() {
         printf("\n|  - bytes used       : %s", sz2a(used_bytes).c_str());
         printf("\n|  - bytes notified   : %s", sz2a(notified_bytes).c_str());
         printf("\n|  - bytes avaiblable : %s", sz2a(avaiblable_bytes).c_str());
         printf("\n|  - bytes            : %s", sz2a(total_bytes).c_str());
         printf("\n|");

         printf("\n|  - objects used       : %lld", used_objects);
         printf("\n|  - objects avaiblable : %lld", avaiblable_objects);
         printf("\n|  - objects notified   : %lld", notified_objects);
         printf("\n|  - objects            : %lld", total_objects);
         printf("\n|");

         printf("\n|  - region count : %llu", region_count);
         printf("\n|");
      }
   };

   tContextStats central_stat;
   auto cstats_length = this->contexts_count;
   auto* cstats = new (alloca(sizeof(tContextStats) * cstats_length)) tContextStats[cstats_length];
   this->central.ForeachObjectRegion(
      [&](ObjectRegion region) {
         auto ctx = region->owner ? region->owner->context : 0;
         auto& stat = ctx ? cstats[ctx->id] : central_stat;
         stat.region_count++;

         stat.used_bytes += region->GetUsedCount() * region->GetObjectSize();
         stat.notified_bytes += region->GetNotifiedCount() * region->GetObjectSize();
         stat.avaiblable_bytes += region->GetAvailablesCount() * region->GetObjectSize();
         stat.total_bytes += region->GetRegionSize();

         stat.used_objects += region->GetUsedCount();
         stat.notified_objects += region->GetNotifiedCount();
         stat.avaiblable_objects += region->GetAvailablesCount();
         stat.total_objects += region->GetCount();

         return true;
      }
   );

   printf("------------------- MemoryCentralContext -------------------\n");
   tContextStats global_stat;
   for (auto context = this->contexts; context; context = context->next.registered) {
      auto& cstat = cstats[context->id];
      global_stat.add(cstat);
      printf("| Context 0x%p ", context);
      if (!context->allocated) printf(" [free]");
      if (context->isShared) printf(" [shared]");
      if (context->thread) {
         printf("\n|  - threadId: %llu", context->thread.GetID());
      }
      printf("\n|");
      cstat.print();
      printf("\n|-------------------------------------------------");
      printf("\n");
   }
   {
      global_stat.add(central_stat);
      printf("| Central");
      central_stat.print();
      printf("\n|-------------------------------------------------");
      printf("\n");
   }
   {
      printf("| Heap:");
      printf("\n|  - context count: %d", cstats_length);
      global_stat.print();
      printf("\n");
   }
   printf("--------------------------------------------------\n");
}

mem::MemoryContext* mem::MemoryController::AcquireContext() {
   {
      std::lock_guard<std::mutex> guard(this->contexts_lock);
      for (auto context = this->contexts; context; context = context->next.registered) {
         if (!context->allocated) {
            context->allocated = true;
            return context;
         }
      }
   }
   {
      auto context = Descriptor::New<MemoryContext>();
      context->allocated = true;
      context->Initiate(&this->central);

      std::lock_guard<std::mutex> guard(this->contexts_lock);
      context->next.registered = this->contexts;
      context->id = this->contexts_count++;
      this->contexts = context;
      return context;
   }
}

mem::MemorySharedContext* mem::MemoryController::CreateSharedContext() {
   auto context = Descriptor::New<MemorySharedContext>();
   context->allocated = true;
   context->Initiate(&this->central);

   std::lock_guard<std::mutex> guard(this->contexts_lock);
   context->next.registered = this->contexts;
   context->id = this->contexts_count++;
   this->contexts = context;
   return context;
}

void mem::MemoryController::DisposeContext(MemoryContext* _context) {
   auto context = static_cast<MemoryContext*>(_context);
   if (context->heap == &this->central && context->allocated) {
      context->allocated = false;
   }
}

void mem::MemoryController::SetTimeStampOption(bool enabled) {
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   ObjectAllocOptions options;
   options.enableTimeStamp = 1;
   if (enabled) {
      for (auto ctx = this->contexts; ctx; ctx = ctx->next.registered)
         ctx->options.enableds |= options.enableds;
   }
   else {
      for (auto ctx = this->contexts; ctx; ctx = ctx->next.registered)
         ctx->options.enableds &= ~options.enableds;
   }
}

void mem::MemoryController::SetStackStampOption(bool enabled) {
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   ObjectAllocOptions options;
   options.enableStackStamp = 1;
   if (enabled) {
      for (auto ctx = this->contexts; ctx; ctx = ctx->next.registered)
         ctx->options.enableds |= options.enableds;
   }
   else {
      for (auto ctx = this->contexts; ctx; ctx = ctx->next.registered)
         ctx->options.enableds &= ~options.enableds;
   }
}

void mem::MemoryController::SetSecurityPaddingOption(uint32_t paddingSize) {
   std::lock_guard<std::mutex> guard(this->contexts_lock);
   for (auto context = this->contexts; context; context = context->next.registered) {
      context->options.enableSecurityPadding = paddingSize;
   }
}

void mem::MemoryController::RescueStarvedConsumer(StarvedConsumerToken& token) {
   {
      std::lock_guard<std::mutex> guard(this->notification_lock);
      token.next = this->starved_consumers;
      this->starved_consumers = &token;
   }
   std::unique_lock<std::mutex> guard(token.lock);
   this->NotifyWorker();
   token.signal.wait(guard);
}

void mem::MemoryController::ScheduleContextRecovery(MemoryContext* context) {
   if (context->next.recovered == none<MemoryContext>()) {
      {
         std::lock_guard<std::mutex> guard(this->notification_lock);
         if (context->next.recovered == none<MemoryContext>()) {
            context->next.recovered = this->recovered_contexts;
            this->recovered_contexts = context;
         }
         else {
            return; // already scheduled
         }
      }
      this->NotifyWorker();
   }
}

void mem::MemoryController::NotifyWorker() {
   this->notification_signal.notify_one();
}
