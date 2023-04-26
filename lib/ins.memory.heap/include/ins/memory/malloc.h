#pragma once
#include <ins/memory/objects-base.h>
#include <ins/memory/schemas.h>

struct ins_memory_stats_t {
   size_t allocated_object_count = 0;
   size_t allocated_object_size = 0;
};

typedef void* (*tp_ins_malloc)(size_t size);
typedef void* (*tp_ins_realloc)(void* ptr, size_t size);
typedef size_t(*tp_ins_msize)(void* ptr);
typedef void (*tp_ins_free)(void*);

extern"C" ins_memory_stats_t ins_get_memory_stats();

extern"C" void* ins_malloc(size_t size);
extern"C" void* ins_calloc(size_t count, size_t size);
extern"C" void* ins_realloc(void* ptr, size_t size, tp_ins_realloc default_realloc = 0);
extern"C" size_t ins_msize(void* ptr, tp_ins_msize default_msize = 0);
extern"C" void ins_free(void* ptr);

