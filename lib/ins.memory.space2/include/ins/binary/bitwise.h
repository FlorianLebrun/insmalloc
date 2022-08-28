#pragma once
#include <stdint.h>

namespace ins {
#ifdef GCC_ASM
#define atomic_or(P, V) __sync_or_and_fetch((P), (V))
#define atomic_and(P, V) __sync_and_and_fetch((P), (V))
#define atomic_add(P, V) __sync_add_and_fetch((P), (V))
#define atomic_xadd(P, V) __sync_fetch_and_add((P), (V))
#define atomic_cmpxchg_bool(P, O, N) __sync_bool_compare_and_swap((P), (O), (N))
#define atomic_access(V) (*(volatile typeof(V) *)&(V))

   static inline int bts(volatile void* mem, size_t offset)
   {
      asm goto (
         "lock; bts %0, (%1)\n"
         "jc %l[carry]\n"
         :
      : "r" (offset), "r" (mem)
         : "memory", "cc"
         : carry);
      return 0;

   carry:
      return 1;
   }

   static inline int btr(volatile void* mem, size_t offset)
   {
      asm goto (
         "lock; btr %0, (%1)\n"
         "jnc %l[ncarry]\n"
         :
      : "r" (offset), "r" (mem)
         : "memory", "cc"
         : ncarry);
      return 1;

   ncarry:
      return 0;
   }

   static inline int lsb_32(unsigned x)
   {
      int result;

      asm("bsf %[x], %[result]"
         : [result] "=r" (result)
         : [x] "mr" (x)
         : "cc");

      return result;
   }

   static inline size_t msb_32(unsigned x)
   {
      size_t result;

      asm("bsr %[x], %[result]"
         : [result] "=r" (result)
         : [x] "mr" (x)
         : "cc");

      return result;
   }

#ifdef __x86_64__
   static inline size_t lsb_64(size_t x)
   {
      size_t result;

      asm("bsfq %[x], %[result]"
         : [result] "=r" (result)
         : [x] "mr" (x)
         : "cc");

      return result;
   }

   static inline size_t flsq(size_t x)
   {
      size_t result;

      asm("bsrq %[x], %[result]"
         : [result] "=r" (result)
         : [x] "mr" (x)
         : "cc");

      return result;
   }

#else
   static inline size_t lsb_64(unsigned long long x)
   {
      size_t result;

      unsigned xlo = x & 0xffffffff;
      unsigned xhi = x >> 32;

      unsigned tmp;

      asm("bsfl %[xhi], %[tmp]\n"
         "addl $0x20, %[tmp]\n"
         "bsfl %[xlo], %[result]\n"
         "cmove %[tmp], %[result]\n"
         : [result] "=r" (result), [tmp] "=&r" (tmp)
         : [xlo] "rm" (xlo), [xhi] "rm" (xhi)
         : "cc");

      return result;
   }

   static inline size_t flsq(unsigned long long x)
   {
      size_t result;

      unsigned xlo = x & 0xffffffff;
      unsigned xhi = x >> 32;
      unsigned tmp;

      asm("bsrl %[xlo], %[tmp]\n"
         "addl $-0x20, %[tmp]\n"
         "bsrl %[xhi], %[result]\n"
         "cmove %[tmp], %[result]\n"
         "addl $0x20, %[result]\n"
         : [result] "=r" (result), [tmp] "=&r" (tmp)
         : [xlo] "rm" (xlo), [xhi] "rm" (xhi)
         : "cc");

      return result;
   }

#endif

   static inline unsigned char xchg_8(void* ptr, unsigned char x)
   {
      asm volatile("xchgb %0,%1"
         :"=r" ((unsigned char)x)
         : "m" (*(volatile unsigned char*)ptr), "0" (x)
         : "memory");

      return x;
   }

   static inline unsigned short xchg_16(void* ptr, unsigned short x)
   {
      asm volatile("xchgw %0,%1"
         :"=r" ((unsigned short)x)
         : "m" (*(volatile unsigned short*)ptr), "0" (x)
         : "memory");

      return x;
   }


   static inline unsigned xchg_32(void* ptr, unsigned x)
   {
      asm volatile("xchgl %0,%1"
         :"=r" ((unsigned)x)
         : "m" (*(volatile unsigned*)ptr), "0" (x)
         : "memory");

      return x;
   }

#ifdef __x86_64__
   static inline unsigned long long xchg_64(void* ptr, unsigned long long x)
   {
      asm volatile("xchgq %0,%1"
         :"=r" ((unsigned long long) x)
         : "m" (*(volatile unsigned long long*)ptr), "0" (x)
         : "memory");

      return x;
   }

   static inline void* xchg_ptr(void* ptr, void* x)
   {
      __asm__ __volatile__("xchgq %0,%1"
         :"=r" ((uintptr_t)x)
         : "m" (*(volatile uintptr_t*)ptr), "0" ((uintptr_t)x)
         : "memory");

      return x;
   }
#else
   static inline void* xchg_ptr(void* ptr, void* x)
   {
      __asm__ __volatile__("xchgl %k0,%1"
         :"=r" ((uintptr_t)x)
         : "m" (*(volatile uintptr_t*)ptr), "0" ((uintptr_t)x)
         : "memory");
      return x;
   }
#endif

   static inline unsigned long long rdtsc(void)
   {
      unsigned hi, lo;
      asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
      return lo + ((unsigned long long)hi << 32);
   }

#else

#include <intrin.h>

   static inline size_t lsb_32(unsigned x)
   {
      unsigned long result;
      __assume(x);
      _BitScanForward(&result, x);

      return result;
   }

   static inline size_t msb_32(unsigned x)
   {
      unsigned long result;
      __assume(x);
      _BitScanReverse(&result, x);

      return result;
   }

#ifdef _WIN64
   static inline size_t lsb_64(unsigned long long x)
   {
      unsigned long result;
      __assume(x);
      _BitScanForward64(&result, x);

      return result;
   }
#else
   // TODO
#endif

#ifdef _WIN64
   static inline size_t msb_64(unsigned long long x)
   {
      unsigned long result;
      __assume(x);
      _BitScanReverse64(&result, x);

      return result;
   }
#else
// TODO
#endif

#ifdef _WIN64
   static inline void* xchg_ptr(void* ptr, void* x)
   {
      return (void*)_InterlockedExchange64((volatile __int64*)ptr, (__int64)x);
   }
#else
   static inline void* xchg_ptr(void* ptr, void* x)
   {
      return (void*)_InterlockedExchange((volatile long*)ptr, (long)x);
   }
#endif


#endif

   static inline uint32_t lmask_32(int nbit) {
      return (uint32_t(1) << nbit) - 1;
   }
   static inline uint64_t lmask_64(int nbit) {
      return (uint64_t(1) << nbit) - 1;
   }

   static inline uint32_t umask_32(int nbit) {
      return ~lmask_32(nbit);
   }
   static inline uint64_t umask_64(int nbit) {
      return ~lmask_64(nbit);
   }

   static inline size_t log2_ceil_32(uint32_t value) {
      auto n = msb_32(value);
      if (value & lmask_32(n)) n++;
      return n;
   }
   static inline size_t log2_ceil_64(uint64_t value) {
      auto n = msb_64(value);
      if (value & lmask_64(n)) n++;
      return n;
   }

}