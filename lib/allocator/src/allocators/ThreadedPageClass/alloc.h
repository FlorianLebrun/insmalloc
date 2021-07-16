/* ----------------------------------------------------------------------------
Copyright (c) 2018-2021, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_H
#define MIMALLOC_H

#include <sat/memory/memory.hpp>
#include <sat/memory/system-object.hpp>

#define MPC_MALLOC_VERSION 171   // major + 2 digits minor

// ------------------------------------------------------
// Compiler specific attributes
// ------------------------------------------------------

#ifdef __cplusplus
#if (__cplusplus >= 201103L) || (_MSC_VER > 1900)  // C++11
#define mpc_attr_noexcept   noexcept
#else
#define mpc_attr_noexcept   throw()
#endif
#else
#define mpc_attr_noexcept
#endif

#if defined(__cplusplus) && (__cplusplus >= 201703)
#define mpc_decl_nodiscard    [[nodiscard]]
#elif (__GNUC__ >= 4) || defined(__clang__)  // includes clang, icc, and clang-cl
#define mpc_decl_nodiscard    __attribute__((warn_unused_result))
#elif (_MSC_VER >= 1700)
#define mpc_decl_nodiscard    _Check_return_
#else
#define mpc_decl_nodiscard
#endif

#if defined(_MSC_VER) || defined(__MINGW32__)
#if !defined(MPC_SHARED_LIB)
#define mpc_decl_export
#elif defined(MPC_SHARED_LIB_EXPORT)
#define mpc_decl_export              __declspec(dllexport)
#else
#define mpc_decl_export              __declspec(dllimport)
#endif
#if defined(__MINGW32__)
#define mpc_decl_restrict
#define mpc_attr_malloc              __attribute__((malloc))
#else
#if (_MSC_VER >= 1900) && !defined(__EDG__)
#define mpc_decl_restrict          __declspec(allocator) __declspec(restrict)
#else
#define mpc_decl_restrict          __declspec(restrict)
#endif
#define mpc_attr_malloc
#endif
#define mpc_cdecl                      __cdecl
#define mpc_attr_alloc_size(s)
#define mpc_attr_alloc_size2(s1,s2)
#define mpc_attr_alloc_align(p)
#elif defined(__GNUC__)                 // includes clang and icc
#define mpc_cdecl                      // leads to warnings... __attribute__((cdecl))
#define mpc_decl_export                __attribute__((visibility("default")))
#define mpc_decl_restrict
#define mpc_attr_malloc                __attribute__((malloc))
#if (defined(__clang_major__) && (__clang_major__ < 4)) || (__GNUC__ < 5)
#define mpc_attr_alloc_size(s)
#define mpc_attr_alloc_size2(s1,s2)
#define mpc_attr_alloc_align(p)
#elif defined(__INTEL_COMPILER)
#define mpc_attr_alloc_size(s)       __attribute__((alloc_size(s)))
#define mpc_attr_alloc_size2(s1,s2)  __attribute__((alloc_size(s1,s2)))
#define mpc_attr_alloc_align(p)
#else
#define mpc_attr_alloc_size(s)       __attribute__((alloc_size(s)))
#define mpc_attr_alloc_size2(s1,s2)  __attribute__((alloc_size(s1,s2)))
#define mpc_attr_alloc_align(p)      __attribute__((alloc_align(p)))
#endif
#else
#define mpc_cdecl
#define mpc_decl_export
#define mpc_decl_restrict
#define mpc_attr_malloc
#define mpc_attr_alloc_size(s)
#define mpc_attr_alloc_size2(s1,s2)
#define mpc_attr_alloc_align(p)
#endif

// ------------------------------------------------------
// Includes
// ------------------------------------------------------

#include <stddef.h>     // size_t
#include <stdbool.h>    // bool

#ifdef __cplusplus
extern "C" {
#endif

   // ------------------------------------------------------
   // Standard malloc interface
   // ------------------------------------------------------

   SAT_API void* mpc_malloc(size_t size);
   SAT_API void mpc_free(void* p);

   // ------------------------------------------------------
   // Extended functionality
   // ------------------------------------------------------
#define MPC_SMALL_WSIZE_MAX  (128)
#define MPC_SMALL_SIZE_MAX   (MPC_SMALL_WSIZE_MAX*sizeof(void*))

// ------------------------------------------------------
// Internals
// ------------------------------------------------------

   typedef void (mpc_cdecl mpc_deferred_free_fun)(bool force, unsigned long long heartbeat, void* arg);
   mpc_decl_export void mpc_register_deferred_free(mpc_deferred_free_fun* deferred_free, void* arg) mpc_attr_noexcept;

   typedef void (mpc_cdecl mpc_output_fun)(const char* msg, void* arg);
   mpc_decl_export void mpc_register_output(mpc_output_fun* out, void* arg) mpc_attr_noexcept;

   typedef void (mpc_cdecl mpc_error_fun)(int err, void* arg);
   mpc_decl_export void mpc_register_error(mpc_error_fun* fun, void* arg);

   mpc_decl_export void mpc_collect(bool force)    mpc_attr_noexcept;
   mpc_decl_export int  mpc_version(void)          mpc_attr_noexcept;

   mpc_decl_export void mpc_process_init(void)     mpc_attr_noexcept;
   mpc_decl_export void mpc_thread_init(void)      mpc_attr_noexcept;
   mpc_decl_export void mpc_thread_done(void)      mpc_attr_noexcept;
   mpc_decl_export void mpc_thread_stats_print_out(mpc_output_fun* out, void* arg) mpc_attr_noexcept;

   // -------------------------------------------------------------------------------------
   // Heaps: first-class, but can only allocate from the same thread that created it.
   // -------------------------------------------------------------------------------------

   struct mpc_heap_s;
   typedef struct mpc_heap_s mpc_heap_t;

   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_heap_malloc(mpc_heap_t* heap, size_t size) mpc_attr_noexcept mpc_attr_malloc mpc_attr_alloc_size(2);
   mpc_decl_nodiscard mpc_decl_export mpc_heap_t* mpc_heap_new(void);
   mpc_decl_export void       mpc_heap_delete(mpc_heap_t* heap);
   mpc_decl_export void       mpc_heap_destroy(mpc_heap_t* heap);
   mpc_decl_export mpc_heap_t* mpc_heap_set_default(mpc_heap_t* heap);
   mpc_decl_export mpc_heap_t* mpc_heap_get_default(void);
   mpc_decl_export mpc_heap_t* mpc_heap_get_backing(void);
   mpc_decl_export void       mpc_heap_collect(mpc_heap_t* heap, bool force) mpc_attr_noexcept;


   // ------------------------------------------------------
   // Analysis
   // ------------------------------------------------------

   mpc_decl_export bool mpc_heap_contains_block(mpc_heap_t* heap, const void* p);
   mpc_decl_export bool mpc_heap_check_owned(mpc_heap_t* heap, const void* p);
   mpc_decl_export bool mpc_check_owned(const void* p);

   // An area of heap space contains blocks of a single size.
   typedef struct mpc_heap_area_s {
      void* blocks;      // start of the area containing heap blocks
      size_t reserved;    // bytes reserved for this area (virtual)
      size_t committed;   // current available bytes for this area
      size_t used;        // bytes in use by allocated blocks
      size_t block_size;  // size in bytes of each block
   } mpc_heap_area_t;

   typedef bool (mpc_cdecl mpc_block_visit_fun)(const mpc_heap_t* heap, const mpc_heap_area_t* area, void* block, size_t block_size, void* arg);

   mpc_decl_export bool mpc_heap_visit_blocks(const mpc_heap_t* heap, bool visit_all_blocks, mpc_block_visit_fun* visitor, void* arg);

   // Experimental
   mpc_decl_nodiscard mpc_decl_export bool mpc_is_in_heap_region(const void* p) mpc_attr_noexcept;
   mpc_decl_nodiscard mpc_decl_export bool mpc_is_redirected(void) mpc_attr_noexcept;

   mpc_decl_export int mpc_reserve_huge_os_pages_interleave(size_t pages, size_t numa_nodes, size_t timeout_msecs) mpc_attr_noexcept;
   mpc_decl_export int mpc_reserve_huge_os_pages_at(size_t pages, int numa_node, size_t timeout_msecs) mpc_attr_noexcept;

   mpc_decl_export int  mpc_reserve_os_memory(size_t size, bool commit, bool allow_large) mpc_attr_noexcept;
   mpc_decl_export bool mpc_manage_os_memory(void* start, size_t size, bool is_committed, bool is_large, bool is_zero, int numa_node) mpc_attr_noexcept;


   // deprecated
   mpc_decl_export int  mpc_reserve_huge_os_pages(size_t pages, double max_secs, size_t* pages_reserved) mpc_attr_noexcept;


   // ------------------------------------------------------
   // Convenience
   // ------------------------------------------------------

#define mpc_malloc_tp(tp)                ((tp*)mpc_malloc(sizeof(tp)))
#define mpc_zalloc_tp(tp)                ((tp*)mpc_zalloc(sizeof(tp)))
#define mpc_calloc_tp(tp,n)              ((tp*)mpc_calloc(n,sizeof(tp)))
#define mpc_mallocn_tp(tp,n)             ((tp*)mpc_mallocn(n,sizeof(tp)))
#define mpc_reallocn_tp(p,tp,n)          ((tp*)mpc_reallocn(p,n,sizeof(tp)))
#define mpc_recalloc_tp(p,tp,n)          ((tp*)mpc_recalloc(p,n,sizeof(tp)))

#define mpc_heap_malloc_tp(hp,tp)        ((tp*)mpc_heap_malloc(hp,sizeof(tp)))
#define mpc_heap_zalloc_tp(hp,tp)        ((tp*)mpc_heap_zalloc(hp,sizeof(tp)))
#define mpc_heap_calloc_tp(hp,tp,n)      ((tp*)mpc_heap_calloc(hp,n,sizeof(tp)))
#define mpc_heap_mallocn_tp(hp,tp,n)     ((tp*)mpc_heap_mallocn(hp,n,sizeof(tp)))
#define mpc_heap_reallocn_tp(hp,p,tp,n)  ((tp*)mpc_heap_reallocn(hp,p,n,sizeof(tp)))
#define mpc_heap_recalloc_tp(hp,p,tp,n)  ((tp*)mpc_heap_recalloc(hp,p,n,sizeof(tp)))


// ------------------------------------------------------
// Options, all `false` by default
// ------------------------------------------------------

   typedef enum mpc_option_e {
      // stable options
      mpc_option_show_errors,
      mpc_option_show_stats,
      mpc_option_verbose,
      // the following options are experimental
      mpc_option_eager_commit,
      mpc_option_eager_region_commit,
      mpc_option_reset_decommits,
      mpc_option_large_os_pages,         // implies eager commit
      mpc_option_segment_cache,
      mpc_option_page_reset,
      mpc_option_abandoned_page_reset,
      mpc_option_segment_reset,
      mpc_option_eager_commit_delay,
      mpc_option_reset_delay,
      mpc_option_use_numa_nodes,
      mpc_option_limit_os_alloc,
      mpc_option_os_tag,
      mpc_option_max_errors,
      mpc_option_max_warnings,
      _mpc_option_last
   } mpc_option_t;


   mpc_decl_nodiscard mpc_decl_export bool mpc_option_is_enabled(mpc_option_t option);
   mpc_decl_export void mpc_option_enable(mpc_option_t option);
   mpc_decl_export void mpc_option_disable(mpc_option_t option);
   mpc_decl_export void mpc_option_set_enabled(mpc_option_t option, bool enable);
   mpc_decl_export void mpc_option_set_enabled_default(mpc_option_t option, bool enable);

   mpc_decl_nodiscard mpc_decl_export long mpc_option_get(mpc_option_t option);
   mpc_decl_export void mpc_option_set(mpc_option_t option, long value);
   mpc_decl_export void mpc_option_set_default(mpc_option_t option, long value);


   // -------------------------------------------------------------------------------------------------------
   // "mi" prefixed implementations of various posix, Unix, Windows, and C++ allocation functions.
   // (This can be convenient when providing overrides of these functions as done in `alloc-override.h`.)
   // note: we use `mpc_cfree` as "checked free" and it checks if the pointer is in our heap before free-ing.
   // -------------------------------------------------------------------------------------------------------

   mpc_decl_export void  mpc_cfree(void* p) mpc_attr_noexcept;
   mpc_decl_export void* mpc__expand(void* p, size_t newsize) mpc_attr_noexcept;
   mpc_decl_nodiscard mpc_decl_export size_t mpc_malloc_size(const void* p)        mpc_attr_noexcept;
   mpc_decl_nodiscard mpc_decl_export size_t mpc_malloc_usable_size(const void* p) mpc_attr_noexcept;

   mpc_decl_export int mpc_posix_memalign(void** p, size_t alignment, size_t size)   mpc_attr_noexcept;
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_memalign(size_t alignment, size_t size) mpc_attr_noexcept mpc_attr_malloc mpc_attr_alloc_size(2) mpc_attr_alloc_align(1);
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_valloc(size_t size)  mpc_attr_noexcept mpc_attr_malloc mpc_attr_alloc_size(1);
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_pvalloc(size_t size) mpc_attr_noexcept mpc_attr_malloc mpc_attr_alloc_size(1);
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_aligned_alloc(size_t alignment, size_t size) mpc_attr_noexcept mpc_attr_malloc mpc_attr_alloc_size(2) mpc_attr_alloc_align(1);

   mpc_decl_nodiscard mpc_decl_export void* mpc_reallocarray(void* p, size_t count, size_t size) mpc_attr_noexcept mpc_attr_alloc_size2(2, 3);
   mpc_decl_nodiscard mpc_decl_export void* mpc_aligned_recalloc(void* p, size_t newcount, size_t size, size_t alignment) mpc_attr_noexcept;
   mpc_decl_nodiscard mpc_decl_export void* mpc_aligned_offset_recalloc(void* p, size_t newcount, size_t size, size_t alignment, size_t offset) mpc_attr_noexcept;

   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict unsigned short* mpc_wcsdup(const unsigned short* s) mpc_attr_noexcept mpc_attr_malloc;
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict unsigned char* mpc_mbsdup(const unsigned char* s)  mpc_attr_noexcept mpc_attr_malloc;
   mpc_decl_export int mpc_dupenv_s(char** buf, size_t* size, const char* name)                      mpc_attr_noexcept;
   mpc_decl_export int mpc_wdupenv_s(unsigned short** buf, size_t* size, const unsigned short* name) mpc_attr_noexcept;

   mpc_decl_export void mpc_free_size(void* p, size_t size)                           mpc_attr_noexcept;
   mpc_decl_export void mpc_free_size_aligned(void* p, size_t size, size_t alignment) mpc_attr_noexcept;
   mpc_decl_export void mpc_free_aligned(void* p, size_t alignment)                   mpc_attr_noexcept;

   // The `mpc_new` wrappers implement C++ semantics on out-of-memory instead of directly returning `NULL`.
   // (and call `std::get_new_handler` and potentially raise a `std::bad_alloc` exception).
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_new(size_t size)                   mpc_attr_malloc mpc_attr_alloc_size(1);
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_new_aligned(size_t size, size_t alignment) mpc_attr_malloc mpc_attr_alloc_size(1) mpc_attr_alloc_align(2);
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_new_nothrow(size_t size)           mpc_attr_noexcept mpc_attr_malloc mpc_attr_alloc_size(1);
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_new_aligned_nothrow(size_t size, size_t alignment) mpc_attr_noexcept mpc_attr_malloc mpc_attr_alloc_size(1) mpc_attr_alloc_align(2);
   mpc_decl_nodiscard mpc_decl_export mpc_decl_restrict void* mpc_new_n(size_t count, size_t size)   mpc_attr_malloc mpc_attr_alloc_size2(1, 2);
   mpc_decl_nodiscard mpc_decl_export void* mpc_new_realloc(void* p, size_t newsize)                mpc_attr_alloc_size(2);
   mpc_decl_nodiscard mpc_decl_export void* mpc_new_reallocn(void* p, size_t newcount, size_t size) mpc_attr_alloc_size2(2, 3);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------------------------
// Implement the C++ std::allocator interface for use in STL containers.
// (note: see `alloc-new-delete.h` for overriding the new/delete operators globally)
// ---------------------------------------------------------------------------------------------
#ifdef __cplusplus

#include <cstdint>     // PTRDIFF_MAX
#if (__cplusplus >= 201103L) || (_MSC_VER > 1900)  // C++11
#include <type_traits> // std::true_type
#include <utility>     // std::forward
#endif

template<class T> struct mpc_stl_allocator {
   typedef T                 value_type;
   typedef std::size_t       size_type;
   typedef std::ptrdiff_t    difference_type;
   typedef value_type& reference;
   typedef value_type const& const_reference;
   typedef value_type* pointer;
   typedef value_type const* const_pointer;
   template <class U> struct rebind { typedef mpc_stl_allocator<U> other; };

   mpc_stl_allocator()                                             mpc_attr_noexcept = default;
   mpc_stl_allocator(const mpc_stl_allocator&)                      mpc_attr_noexcept = default;
   template<class U> mpc_stl_allocator(const mpc_stl_allocator<U>&) mpc_attr_noexcept { }
   mpc_stl_allocator  select_on_container_copy_construction() const { return *this; }
   void              deallocate(T* p, size_type) { mpc_free(p); }

#if (__cplusplus >= 201703L)  // C++17
   mpc_decl_nodiscard T* allocate(size_type count) { return static_cast<T*>(mpc_new_n(count, sizeof(T))); }
   mpc_decl_nodiscard T* allocate(size_type count, const void*) { return allocate(count); }
#else
   mpc_decl_nodiscard pointer allocate(size_type count, const void* = 0) { return static_cast<pointer>(mpc_new_n(count, sizeof(value_type))); }
#endif

#if ((__cplusplus >= 201103L) || (_MSC_VER > 1900))  // C++11
   using propagate_on_container_copy_assignment = std::true_type;
   using propagate_on_container_move_assignment = std::true_type;
   using propagate_on_container_swap = std::true_type;
   using is_always_equal = std::true_type;
   template <class U, class ...Args> void construct(U* p, Args&& ...args) { ::new(p) U(std::forward<Args>(args)...); }
   template <class U> void destroy(U* p) mpc_attr_noexcept { p->~U(); }
#else
   void construct(pointer p, value_type const& val) { ::new(p) value_type(val); }
   void destroy(pointer p) { p->~value_type(); }
#endif

   size_type     max_size() const mpc_attr_noexcept { return (PTRDIFF_MAX / sizeof(value_type)); }
   pointer       address(reference x) const { return &x; }
   const_pointer address(const_reference x) const { return &x; }
};

template<class T1, class T2> bool operator==(const mpc_stl_allocator<T1>&, const mpc_stl_allocator<T2>&) mpc_attr_noexcept { return true; }
template<class T1, class T2> bool operator!=(const mpc_stl_allocator<T1>&, const mpc_stl_allocator<T2>&) mpc_attr_noexcept { return false; }
#endif // __cplusplus

#endif
