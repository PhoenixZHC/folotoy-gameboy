#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gb_port.h"

#define GB_STORAGE_MAX_ROMS 16

typedef struct {
    char name[32];
    uint32_t offset;
    uint32_t size;
    uint8_t sha256[32];
} gb_rom_entry_t;

bool gb_storage_init(void);
bool gb_storage_catalog_trusted(void);
size_t gb_storage_count(void);
const gb_rom_entry_t *gb_storage_entry(size_t index);
bool gb_storage_open(size_t index, gb_rom_source_t *out);

typedef int (*gb_storage_reader_t)(void *context, void *dst, size_t max_bytes);
// Must be called outside a running game. The upload becomes visible only after
// all bytes and the ROM header pass validation and a new directory is committed.
bool gb_storage_upload(const char *name, size_t bytes,
                       gb_storage_reader_t reader, void *context);
bool gb_storage_delete(size_t index);
bool gb_storage_rename(size_t index, const char *name);
size_t gb_storage_available(void); // minimum of contiguous free region and remaining quota
size_t gb_storage_free_bytes(void); // total unoccupied ROM capacity
size_t gb_storage_used(void);
size_t gb_storage_limit(void);
