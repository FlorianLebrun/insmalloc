/* ----------------------------------------------------------------------------
Copyright (c) 2018-2021, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "alloc.h"
#include "alloc-internal.h"
#include "alloc-atomic.h"

#include <stdio.h>
#include <stdlib.h> // strtol
#include <string.h> // strncpy, strncat, strlen, strstr
#include <ctype.h>  // toupper
#include <stdarg.h>

#ifdef _MSC_VER
#pragma warning(disable:4996)   // strncpy, strncat
#endif


static uintptr_t mpc_max_error_count = 16; // stop outputting errors after this
static uintptr_t mpc_max_warning_count = 16; // stop outputting warnings after this

static void mpc_add_stderr_output();

int mpc_version(void) mpc_attr_noexcept {
   return MPC_MALLOC_VERSION;
}

#ifdef _WIN32
#include <conio.h>
#endif

// --------------------------------------------------------
// Options
// These can be accessed by multiple threads and may be
// concurrently initialized, but an initializing data race
// is ok since they resolve to the same value.
// --------------------------------------------------------
typedef enum mpc_init_e {
   UNINIT,       // not yet initialized
   DEFAULTED,    // not found in the environment, use default value
   INITIALIZED   // found in environment or set explicitly
} mpc_init_t;

typedef struct mpc_option_desc_s {
   long        value;  // the value
   mpc_init_t   init;   // is it initialized yet? (from the environment)
   mpc_option_t option; // for debugging: the option index should match the option
   const char* name;   // option name without `alloc_` prefix
} mpc_option_desc_t;

#define MPC_OPTION(opt)        mpc_option_##opt, #opt
#define MPC_OPTION_DESC(opt)   {0, UNINIT, MPC_OPTION(opt) }

static mpc_option_desc_t options[_mpc_option_last] =
{
   // stable options
   { 0, UNINIT, MPC_OPTION(show_errors) },
   { 0, UNINIT, MPC_OPTION(show_stats) },
   { 0, UNINIT, MPC_OPTION(verbose) },

   // the following options are experimental and not all combinations make sense.
   { 1, UNINIT, MPC_OPTION(eager_commit) },        // commit per segment directly (4MiB)  (but see also `eager_commit_delay`)
   #if defined(_WIN32) || (MPC_INTPTR_SIZE <= 4)   // and other OS's without overcommit?
   { 0, UNINIT, MPC_OPTION(eager_region_commit) },
   { 1, UNINIT, MPC_OPTION(reset_decommits) },     // reset decommits memory
   #else
   { 1, UNINIT, MPC_OPTION(eager_region_commit) },
   { 0, UNINIT, MPC_OPTION(reset_decommits) },     // reset uses MADV_FREE/MADV_DONTNEED
   #endif
   { 0, UNINIT, MPC_OPTION(large_os_pages) },      // use large OS pages, use only with eager commit to prevent fragmentation of VMA's
   { 0, UNINIT, MPC_OPTION(segment_cache) },       // cache N segments per thread
   { 1, UNINIT, MPC_OPTION(page_reset) },          // reset page memory on free
   { 0, UNINIT, MPC_OPTION(abandoned_page_reset) },// reset free page memory when a thread terminates
   { 0, UNINIT, MPC_OPTION(segment_reset) },       // reset segment memory on free (needs eager commit)
 #if defined(__NetBSD__)
   { 0, UNINIT, MPC_OPTION(eager_commit_delay) },  // the first N segments per thread are not eagerly committed
 #else
   { 1, UNINIT, MPC_OPTION(eager_commit_delay) },  // the first N segments per thread are not eagerly committed (but per page in the segment on demand)
 #endif
   { 100, UNINIT, MPC_OPTION(reset_delay) },       // reset delay in milli-seconds
   { 0,   UNINIT, MPC_OPTION(use_numa_nodes) },    // 0 = use available numa nodes, otherwise use at most N nodes.
   { 0,   UNINIT, MPC_OPTION(limit_os_alloc) },    // 1 = do not use OS memory for allocation (but only reserved arenas)
   { 100, UNINIT, MPC_OPTION(os_tag) },            // only apple specific for now but might serve more or less related purpose
   { 16,  UNINIT, MPC_OPTION(max_errors) },        // maximum errors that are output
   { 16,  UNINIT, MPC_OPTION(max_warnings) }       // maximum warnings that are output

};

static void mpc_option_init(mpc_option_desc_t* desc);

void _mpc_options_init(void) {
   // called on process load; should not be called before the CRT is initialized!
   // (e.g. do not call this from process_init as that may run before CRT initialization)
   mpc_add_stderr_output(); // now it safe to use stderr for output
   for (int i = 0; i < _mpc_option_last; i++) {
      mpc_option_t option = (mpc_option_t)i;
      long l = mpc_option_get(option); // initialize
      if (option != mpc_option_verbose) {
         mpc_option_desc_t* desc = &options[option];
         _mpc_verbose_message("option '%s': %ld\n", desc->name, desc->value);
      }
   }
   mpc_max_error_count = mpc_option_get(mpc_option_max_errors);
   mpc_max_warning_count = mpc_option_get(mpc_option_max_warnings);
}

long mpc_option_get(mpc_option_t option) {
   mpc_option_desc_t* desc = &options[option];
   if (mpc_unlikely(desc->init == UNINIT)) {
      mpc_option_init(desc);
   }
   return desc->value;
}

void mpc_option_set(mpc_option_t option, long value) {
   mpc_option_desc_t* desc = &options[option];
   desc->value = value;
   desc->init = INITIALIZED;
}

void mpc_option_set_default(mpc_option_t option, long value) {
   mpc_option_desc_t* desc = &options[option];
   if (desc->init != INITIALIZED) {
      desc->value = value;
   }
}

bool mpc_option_is_enabled(mpc_option_t option) {
   return (mpc_option_get(option) != 0);
}

void mpc_option_set_enabled(mpc_option_t option, bool enable) {
   mpc_option_set(option, (enable ? 1 : 0));
}

void mpc_option_set_enabled_default(mpc_option_t option, bool enable) {
   mpc_option_set_default(option, (enable ? 1 : 0));
}

void mpc_option_enable(mpc_option_t option) {
   mpc_option_set_enabled(option, true);
}

void mpc_option_disable(mpc_option_t option) {
   mpc_option_set_enabled(option, false);
}


static void mpc_out_stderr(const char* msg, void* arg) {
#ifdef _WIN32
   // on windows with redirection, the C runtime cannot handle locale dependent output
   // after the main thread closes so we use direct console output.
   if (!_mpc_preloading()) { _cputs(msg); }
#else
   fputs(msg, stderr);
#endif
}

// Since an output function can be registered earliest in the `main`
// function we also buffer output that happens earlier. When
// an output function is registered it is called immediately with
// the output up to that point.
#ifndef MPC_MAX_DELAY_OUTPUT
#define MPC_MAX_DELAY_OUTPUT ((uintptr_t)(32*1024))
#endif
static char out_buf[MPC_MAX_DELAY_OUTPUT + 1];
static _Atomic(uintptr_t)out_len;

static void mpc_out_buf(const char* msg, void* arg) {
   if (msg == NULL) return;
   if (mpc_atomic_load_relaxed(&out_len) >= MPC_MAX_DELAY_OUTPUT) return;
   size_t n = strlen(msg);
   if (n == 0) return;
   // claim space
   uintptr_t start = mpc_atomic_add_acq_rel(&out_len, n);
   if (start >= MPC_MAX_DELAY_OUTPUT) return;
   // check bound
   if (start + n >= MPC_MAX_DELAY_OUTPUT) {
      n = MPC_MAX_DELAY_OUTPUT - start - 1;
   }
   _mpc_memcpy(&out_buf[start], msg, n);
}

static void mpc_out_buf_flush(mpc_output_fun* out, bool no_more_buf, void* arg) {
   if (out == NULL) return;
   // claim (if `no_more_buf == true`, no more output will be added after this point)
   size_t count = mpc_atomic_add_acq_rel(&out_len, (no_more_buf ? MPC_MAX_DELAY_OUTPUT : 1));
   // and output the current contents
   if (count > MPC_MAX_DELAY_OUTPUT) count = MPC_MAX_DELAY_OUTPUT;
   out_buf[count] = 0;
   out(out_buf, arg);
   if (!no_more_buf) {
      out_buf[count] = '\n'; // if continue with the buffer, insert a newline
   }
}


// Once this module is loaded, switch to this routine
// which outputs to stderr and the delayed output buffer.
static void mpc_out_buf_stderr(const char* msg, void* arg) {
   mpc_out_stderr(msg, arg);
   mpc_out_buf(msg, arg);
}



// --------------------------------------------------------
// Default output handler
// --------------------------------------------------------

// Should be atomic but gives errors on many platforms as generally we cannot cast a function pointer to a uintptr_t.
// For now, don't register output from multiple threads.
static mpc_output_fun* volatile mpc_out_default; // = NULL
static _Atomic(void*)mpc_out_arg; // = NULL

static mpc_output_fun* mpc_out_get_default(void** parg) {
   if (parg != NULL) { *parg = mpc_atomic_load_ptr_acquire(void, &mpc_out_arg); }
   mpc_output_fun* out = mpc_out_default;
   return (out == NULL ? &mpc_out_buf : out);
}

void mpc_register_output(mpc_output_fun* out, void* arg) mpc_attr_noexcept {
   mpc_out_default = (out == NULL ? &mpc_out_stderr : out); // stop using the delayed output buffer
   mpc_atomic_store_ptr_release(void, &mpc_out_arg, arg);
   if (out != NULL) mpc_out_buf_flush(out, true, arg);         // output all the delayed output now
}

// add stderr to the delayed output after the module is loaded
static void mpc_add_stderr_output() {
   mpc_out_buf_flush(&mpc_out_stderr, false, NULL); // flush current contents to stderr
   mpc_out_default = &mpc_out_buf_stderr;           // and add stderr to the delayed output
}

// --------------------------------------------------------
// Messages, all end up calling `_mpc_fputs`.
// --------------------------------------------------------
static _Atomic(uintptr_t)error_count;   // = 0;  // when >= max_error_count stop emitting errors
static _Atomic(uintptr_t)warning_count; // = 0;  // when >= max_warning_count stop emitting warnings

// When overriding malloc, we may recurse into mpc_vfprintf if an allocation
// inside the C runtime causes another message.
static mpc_decl_thread bool recurse = false;

static bool mpc_recurse_enter(void) {
#if defined(__APPLE__) || defined(MPC_TLS_RECURSE_GUARD)
   if (_mpc_preloading()) return true;
#endif
   if (recurse) return false;
   recurse = true;
   return true;
}

static void mpc_recurse_exit(void) {
#if defined(__APPLE__) || defined(MPC_TLS_RECURSE_GUARD)
   if (_mpc_preloading()) return;
#endif
   recurse = false;
}

void _mpc_fputs(mpc_output_fun* out, void* arg, const char* prefix, const char* message) {
   if (out == NULL || (FILE*)out == stdout || (FILE*)out == stderr) { // TODO: use mpc_out_stderr for stderr?
      if (!mpc_recurse_enter()) return;
      out = mpc_out_get_default(&arg);
      if (prefix != NULL) out(prefix, arg);
      out(message, arg);
      mpc_recurse_exit();
   }
   else {
      if (prefix != NULL) out(prefix, arg);
      out(message, arg);
   }
}

// Define our own limited `fprintf` that avoids memory allocation.
// We do this using `snprintf` with a limited buffer.
static void mpc_vfprintf(mpc_output_fun* out, void* arg, const char* prefix, const char* fmt, va_list args) {
   char buf[512];
   if (fmt == NULL) return;
   if (!mpc_recurse_enter()) return;
   vsnprintf(buf, sizeof(buf) - 1, fmt, args);
   mpc_recurse_exit();
   _mpc_fputs(out, arg, prefix, buf);
}

void _mpc_fprintf(mpc_output_fun* out, void* arg, const char* fmt, ...) {
   va_list args;
   va_start(args, fmt);
   mpc_vfprintf(out, arg, NULL, fmt, args);
   va_end(args);
}

void _mpc_trace_message(const char* fmt, ...) {
   if (mpc_option_get(mpc_option_verbose) <= 1) return;  // only with verbose level 2 or higher
   va_list args;
   va_start(args, fmt);
   mpc_vfprintf(NULL, NULL, "alloc: ", fmt, args);
   va_end(args);
}

void _mpc_verbose_message(const char* fmt, ...) {
   if (!mpc_option_is_enabled(mpc_option_verbose)) return;
   va_list args;
   va_start(args, fmt);
   mpc_vfprintf(NULL, NULL, "alloc: ", fmt, args);
   va_end(args);
}

static void mpc_show_error_message(const char* fmt, va_list args) {
   if (!mpc_option_is_enabled(mpc_option_show_errors) && !mpc_option_is_enabled(mpc_option_verbose)) return;
   if (mpc_atomic_increment_acq_rel(&error_count) > mpc_max_error_count) return;
   mpc_vfprintf(NULL, NULL, "alloc: error: ", fmt, args);
}

void _mpc_warning_message(const char* fmt, ...) {
   if (!mpc_option_is_enabled(mpc_option_show_errors) && !mpc_option_is_enabled(mpc_option_verbose)) return;
   if (mpc_atomic_increment_acq_rel(&warning_count) > mpc_max_warning_count) return;
   va_list args;
   va_start(args, fmt);
   mpc_vfprintf(NULL, NULL, "alloc: warning: ", fmt, args);
   va_end(args);
}

// --------------------------------------------------------
// Errors
// --------------------------------------------------------

static mpc_error_fun* volatile  mpc_error_handler; // = NULL
static _Atomic(void*)mpc_error_arg;     // = NULL

static void mpc_error_default(int err) {
}

void mpc_register_error(mpc_error_fun* fun, void* arg) {
   mpc_error_handler = fun;  // can be NULL
   mpc_atomic_store_ptr_release(void, &mpc_error_arg, arg);
}

void _mpc_error_message(int err, const char* fmt, ...) {
   // show detailed error message
   va_list args;
   va_start(args, fmt);
   mpc_show_error_message(fmt, args);
   va_end(args);
   // and call the error handler which may abort (or return normally)
   if (mpc_error_handler != NULL) {
      mpc_error_handler(err, mpc_atomic_load_ptr_acquire(void, &mpc_error_arg));
   }
   else {
      mpc_error_default(err);
   }
}

// --------------------------------------------------------
// Initialize options by checking the environment
// --------------------------------------------------------

static void mpc_strlcpy(char* dest, const char* src, size_t dest_size) {
   dest[0] = 0;
   strncpy(dest, src, dest_size - 1);
   dest[dest_size - 1] = 0;
}

static void mpc_strlcat(char* dest, const char* src, size_t dest_size) {
   strncat(dest, src, dest_size - 1);
   dest[dest_size - 1] = 0;
}

static inline int mpc_strnicmp(const char* s, const char* t, size_t n) {
   if (n == 0) return 0;
   for (; *s != 0 && *t != 0 && n > 0; s++, t++, n--) {
      if (toupper(*s) != toupper(*t)) break;
   }
   return (n == 0 ? 0 : *s - *t);
}

#if defined _WIN32
// On Windows use GetEnvironmentVariable instead of getenv to work
// reliably even when this is invoked before the C runtime is initialized.
// i.e. when `_mpc_preloading() == true`.
// Note: on windows, environment names are not case sensitive.
#include <windows.h>
static bool mpc_getenv(const char* name, char* result, size_t result_size) {
   result[0] = 0;
   size_t len = GetEnvironmentVariableA(name, result, (DWORD)result_size);
   return (len > 0 && len < result_size);
}
#elif !defined(MPC_USE_ENVIRON) || (MPC_USE_ENVIRON!=0)
// On Posix systemsr use `environ` to acces environment variables 
// even before the C runtime is initialized.
#if defined(__APPLE__) && defined(__has_include) && __has_include(<crt_externs.h>)
#include <crt_externs.h>
static char** mpc_get_environ(void) {
   return (*_NSGetEnviron());
}
#else 
extern char** environ;
static char** mpc_get_environ(void) {
   return environ;
}
#endif
static bool mpc_getenv(const char* name, char* result, size_t result_size) {
   if (name == NULL) return false;
   const size_t len = strlen(name);
   if (len == 0) return false;
   char** env = mpc_get_environ();
   if (env == NULL) return false;
   // compare up to 256 entries
   for (int i = 0; i < 256 && env[i] != NULL; i++) {
      const char* s = env[i];
      if (mpc_strnicmp(name, s, len) == 0 && s[len] == '=') { // case insensitive
        // found it
         mpc_strlcpy(result, s + len + 1, result_size);
         return true;
      }
   }
   return false;
}
#else  
// fallback: use standard C `getenv` but this cannot be used while initializing the C runtime
static bool mpc_getenv(const char* name, char* result, size_t result_size) {
   // cannot call getenv() when still initializing the C runtime.
   if (_mpc_preloading()) return false;
   const char* s = getenv(name);
   if (s == NULL) {
      // we check the upper case name too.
      char buf[64 + 1];
      size_t len = strlen(name);
      if (len >= sizeof(buf)) len = sizeof(buf) - 1;
      for (size_t i = 0; i < len; i++) {
         buf[i] = toupper(name[i]);
      }
      buf[len] = 0;
      s = getenv(buf);
   }
   if (s != NULL && strlen(s) < result_size) {
      mpc_strlcpy(result, s, result_size);
      return true;
   }
   else {
      return false;
   }
}
#endif

static void mpc_option_init(mpc_option_desc_t* desc) {
   // Read option value from the environment
   char buf[64 + 1];
   mpc_strlcpy(buf, "alloc_", sizeof(buf));
   mpc_strlcat(buf, desc->name, sizeof(buf));
   char s[64 + 1];
   if (mpc_getenv(buf, s, sizeof(s))) {
      size_t len = strlen(s);
      if (len >= sizeof(buf)) len = sizeof(buf) - 1;
      for (size_t i = 0; i < len; i++) {
         buf[i] = (char)toupper(s[i]);
      }
      buf[len] = 0;
      if (buf[0] == 0 || strstr("1;TRUE;YES;ON", buf) != NULL) {
         desc->value = 1;
         desc->init = INITIALIZED;
      }
      else if (strstr("0;FALSE;NO;OFF", buf) != NULL) {
         desc->value = 0;
         desc->init = INITIALIZED;
      }
      else {
         char* end = buf;
         long value = strtol(buf, &end, 10);
         if (*end == 0) {
            desc->value = value;
            desc->init = INITIALIZED;
         }
         else {
            _mpc_warning_message("environment option alloc_%s has an invalid value: %s\n", desc->name, buf);
            desc->init = DEFAULTED;
         }
      }
   }
   else if (!_mpc_preloading()) {
      desc->init = DEFAULTED;
   }
}
