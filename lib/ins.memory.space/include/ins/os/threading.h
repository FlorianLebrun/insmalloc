#pragma once
#include <stdint.h>

namespace ins::os {
   struct Thread {
      Thread();
      Thread(Thread&& x);
      ~Thread();
      void operator = (Thread&& x);
      operator bool();

      uint64_t GetID();
      bool IsCurrent();
      void Suspend();
      void Resume();
      void Clear();
      static Thread current();
   private:
      uint64_t d0, d1;
   };
}
