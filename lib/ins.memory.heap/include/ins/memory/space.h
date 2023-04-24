#pragma once
#include <ins/memory/structs.h>
#include <ins/binary/alignment.h>
#include <ins/binary/bitwise.h>
#include <ins/memory/regions.h>
#include <ins/memory/contexts.h>

typedef void* (*tp_ins_malloc)(size_t size);
typedef void* (*tp_ins_realloc)(void* ptr, size_t size);
typedef size_t(*tp_ins_msize)(void* ptr);
typedef void (*tp_ins_free)(void*);

extern"C" ins::mem::MemoryContext * ins_get_thread_context();

extern"C" bool ins_check_overflow(void* ptr);
extern"C" bool ins_get_metadata(void* ptr, ins::mem::ObjectAnalyticsInfos & meta);
extern"C" size_t ins_msize(void* ptr, tp_ins_msize default_msize = 0);

extern"C" void* ins_malloc(size_t size);
extern"C" void* ins_calloc(size_t count, size_t size);
extern"C" void* ins_realloc(void* ptr, size_t size, tp_ins_realloc default_realloc = 0);
extern"C" void ins_free(void* ptr);

extern"C" ins::mem::ObjectHeader ins_new(size_t size);
extern"C" ins::mem::ObjectHeader ins_new_unmanaged(ins::mem::SchemaID schemaID);
extern"C" ins::mem::ObjectHeader ins_new_managed(ins::mem::SchemaID schemaID);
extern"C" void ins_delete(ins::mem::ObjectHeader obj);
