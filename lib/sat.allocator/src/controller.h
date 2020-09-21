#pragma once
#include "./heaps/heaps.h"

namespace sat {

   struct ControllerImpl : sat::Controller {

      bool enableObjectTracing;
      bool enableObjectStackTracing;
      bool enableObjectTimeTracing;

      SpinLock heaps_lock;
      GlobalHeap* heaps_list;
      GlobalHeap* heaps_table[256];

      SpinLock threads_lock;
      sat::Thread* threads_list;

      void initialize();

      // Heap management
      virtual sat::Heap* createHeap(sat::tHeapType type, const char* name) override final;
      virtual sat::Heap* getHeap(int id) override final;

      // Analysis
      virtual void traverseObjects(sat::IObjectVisitor* visitor, uintptr_t target_address) override final;
      virtual bool checkObjectsOverflow() override final;
   };
}

extern sat::ControllerImpl g_SAT;
