/* ----------------------------------------------------------------------------
Copyright (c) 2018-2021 Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_ATOMIC_H
#define MIMALLOC_ATOMIC_H

// --------------------------------------------------------------------------------------------
// Atomics
// We need to be portable between C, C++, and MSVC.
// We base the primitives on the C/C++ atomics and create a mimimal wrapper for MSVC in C compilation mode. 
// This is why we try to use only `uintptr_t` and `<type>*` as atomic types. 
// To gain better insight in the range of used atomics, we use explicitly named memory order operations 
// instead of passing the memory order as a parameter.
// -----------------------------------------------------------------------------------------------

// Use C++ atomics
#include <thread>
#include <atomic>
#define  _Atomic(tp)            std::atomic<tp>

// Various defines for all used memory orders in alloc
#define mpc_atomic_cas_weak(p,expected,desired,mem_success,mem_fail)  \
  std::atomic_compare_exchange_weak_explicit(p,expected,desired,mem_success,mem_fail)

#define mpc_atomic_cas_strong(p,expected,desired,mem_success,mem_fail)  \
  std::atomic_compare_exchange_strong_explicit(p,expected,desired,mem_success,mem_fail)

#define mpc_atomic_load_acquire(p)                std::atomic_load_explicit(p,std::memory_order_acquire)
#define mpc_atomic_load_relaxed(p)                std::atomic_load_explicit(p,std::memory_order_relaxed)
#define mpc_atomic_store_release(p,x)             std::atomic_store_explicit(p,x,std::memory_order_release)
#define mpc_atomic_store_relaxed(p,x)             std::atomic_store_explicit(p,x,std::memory_order_relaxed)
#define mpc_atomic_exchange_release(p,x)          std::atomic_exchange_explicit(p,x,std::memory_order_release)
#define mpc_atomic_exchange_acq_rel(p,x)          std::atomic_exchange_explicit(p,x,std::memory_order_acq_rel)
#define mpc_atomic_cas_weak_release(p,exp,des)    mpc_atomic_cas_weak(p,exp,des,std::memory_order_release,std::memory_order_relaxed)
#define mpc_atomic_cas_weak_acq_rel(p,exp,des)    mpc_atomic_cas_weak(p,exp,des,std::memory_order_acq_rel,std::memory_order_acquire)
#define mpc_atomic_cas_strong_release(p,exp,des)  mpc_atomic_cas_strong(p,exp,des,std::memory_order_release,std::memory_order_relaxed)
#define mpc_atomic_cas_strong_acq_rel(p,exp,des)  mpc_atomic_cas_strong(p,exp,des,std::memory_order_acq_rel,std::memory_order_acquire)

#define mpc_atomic_add_relaxed(p,x)               std::atomic_fetch_add_explicit(p,x,std::memory_order_relaxed)
#define mpc_atomic_sub_relaxed(p,x)               std::atomic_fetch_sub_explicit(p,x,std::memory_order_relaxed)
#define mpc_atomic_add_acq_rel(p,x)               std::atomic_fetch_add_explicit(p,x,std::memory_order_acq_rel)
#define mpc_atomic_sub_acq_rel(p,x)               std::atomic_fetch_sub_explicit(p,x,std::memory_order_acq_rel)
#define mpc_atomic_and_acq_rel(p,x)               std::atomic_fetch_and_explicit(p,x,std::memory_order_acq_rel)
#define mpc_atomic_or_acq_rel(p,x)                std::atomic_fetch_or_explicit(p,x,std::memory_order_acq_rel)

#define mpc_atomic_increment_relaxed(p)           mpc_atomic_add_relaxed(p,(uintptr_t)1)
#define mpc_atomic_decrement_relaxed(p)           mpc_atomic_sub_relaxed(p,(uintptr_t)1)
#define mpc_atomic_increment_acq_rel(p)           mpc_atomic_add_acq_rel(p,(uintptr_t)1)
#define mpc_atomic_decrement_acq_rel(p)           mpc_atomic_sub_acq_rel(p,(uintptr_t)1)

static inline intptr_t mpc_atomic_addi(_Atomic(intptr_t)*p, intptr_t add);
static inline intptr_t mpc_atomic_subi(_Atomic(intptr_t)*p, intptr_t sub);

// In C++/C11 atomics we have polymorphic atomics so can use the typed `ptr` variants (where `tp` is the type of atomic value)
// We use these macros so we can provide a typed wrapper in MSVC in C compilation mode as well
#define mpc_atomic_load_ptr_acquire(tp,p)                mpc_atomic_load_acquire(p)
#define mpc_atomic_load_ptr_relaxed(tp,p)                mpc_atomic_load_relaxed(p)

// In C++ we need to add casts to help resolve templates if NULL is passed
#define mpc_atomic_store_ptr_release(tp,p,x)             mpc_atomic_store_release(p,(tp*)x)
#define mpc_atomic_store_ptr_relaxed(tp,p,x)             mpc_atomic_store_relaxed(p,(tp*)x)
#define mpc_atomic_cas_ptr_weak_release(tp,p,exp,des)    mpc_atomic_cas_weak_release(p,exp,(tp*)des)
#define mpc_atomic_cas_ptr_weak_acq_rel(tp,p,exp,des)    mpc_atomic_cas_weak_acq_rel(p,exp,(tp*)des)
#define mpc_atomic_cas_ptr_strong_release(tp,p,exp,des)  mpc_atomic_cas_strong_release(p,exp,(tp*)des)
#define mpc_atomic_exchange_ptr_release(tp,p,x)          mpc_atomic_exchange_release(p,(tp*)x)
#define mpc_atomic_exchange_ptr_acq_rel(tp,p,x)          mpc_atomic_exchange_acq_rel(p,(tp*)x)

// These are used by the statistics
static inline int64_t mpc_atomic_addi64_relaxed(volatile int64_t* p, int64_t add) {
   return std::atomic_fetch_add_explicit((_Atomic(int64_t)*)p, add, std::memory_order_relaxed);
}
static inline void mpc_atomic_maxi64_relaxed(volatile int64_t* p, int64_t x) {
   int64_t current = mpc_atomic_load_relaxed((_Atomic(int64_t)*)p);
   while (current < x && !mpc_atomic_cas_weak_release((_Atomic(int64_t)*)p, &current, x)) { /* nothing */ };
}


// Atomically add a signed value; returns the previous value.
static inline intptr_t mpc_atomic_addi(_Atomic(intptr_t)*p, intptr_t add) {
   return (intptr_t)mpc_atomic_add_acq_rel((_Atomic(uintptr_t)*)p, (uintptr_t)add);
}

// Atomically subtract a signed value; returns the previous value.
static inline intptr_t mpc_atomic_subi(_Atomic(intptr_t)*p, intptr_t sub) {
   return (intptr_t)mpc_atomic_addi(p, -sub);
}

#endif // __MIMALLOC_ATOMIC_H
