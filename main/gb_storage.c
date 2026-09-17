#include "gb_storage.h"
#include "gb_name.h"

#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"

#define DIRECTORY_SECTORS 2
#define SECTOR_BYTES 4096u
#define USER_ROM_LIMIT (3u * 1024u * 1024u)

typedef struct {
    char magic[4];
    uint32_t generation;
    uint16_t count;
    uint16_t reserved;
    gb_rom_entry_t entries[GB_STORAGE_MAX_ROMS];
    uint8_t digest[32];
} directory_t;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t count;
} rom_header_t;

_Static_assert(sizeof(gb_rom_entry_t) == 72, "ROM entry layout changed");

static const esp_partition_t *s_partition;
static const char *TAG = "gb_storage";
static gb_rom_entry_t s_entries[GB_STORAGE_MAX_ROMS];
static size_t s_count;
static const gb_rom_entry_t *s_selected;
static bool s_mapped;
static esp_partition_mmap_handle_t s_mapped_handle;
static int s_directory_slot = -1;
static uint32_t s_generation;
static bool s_catalog_trusted;

static uint32_t data_end(void) {
    return s_partition->size - DIRECTORY_SECTORS * SECTOR_BYTES;
}

static void unmap_selected(void) {
    if (s_mapped) {
        esp_partition_munmap(s_mapped_handle);
        s_mapped = false;
    }
    s_selected = NULL;
}

static bool valid_range(uint32_t length, uint32_t offset, uint32_t bytes) {
    return offset <= length && bytes <= length - offset;
}

static bool entries_valid(const gb_rom_entry_t *entries, size_t count, uint32_t end) {
    if (count > GB_STORAGE_MAX_ROMS) return false;
    for (size_t i = 0; i < count; i++) {
        const gb_rom_entry_t *entry = &entries[i];
        if (!gb_name_valid(entry->name) ||
            entry->offset % SECTOR_BYTES || entry->offset < SECTOR_BYTES ||
            entry->size < 32768 || entry->size % 16384 ||
            !valid_range(end, entry->offset, entry->size)) return false;
        for (size_t j = 0; j < i; j++) {
            const gb_rom_entry_t *other = &entries[j];
            if (!strcmp(entry->name, other->name) ||
                (entry->offset < other->offset + other->size &&
                 other->offset < entry->offset + entry->size)) return false;
        }
    }
    return true;
}

static bool read_directory(int slot, directory_t *out) {
    uint32_t offset = data_end() + (uint32_t)slot * SECTOR_BYTES;
    if (esp_partition_read(s_partition, offset, out, sizeof(*out)) != ESP_OK ||
        memcmp(out->magic, "FGB2", 4) ||
        !entries_valid(out->entries, out->count, data_end())) return false;
    uint8_t digest[32];
    if (mbedtls_sha256((const unsigned char *)out,
                       offsetof(directory_t, digest), digest, 0) != 0) return false;
    return memcmp(digest, out->digest, sizeof(digest)) == 0;
}

bool gb_storage_init(void) {
    unmap_selected();
    s_count = 0;
    s_catalog_trusted = false;
    s_directory_slot = -1;
    s_generation = 0;
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                            (esp_partition_subtype_t)0x40, "roms");
    if (!s_partition) return false;
    directory_t *dir = malloc(sizeof(*dir));
    if (!dir) return false;
    for (int slot = 0; slot < DIRECTORY_SECTORS; slot++) {
        if (read_directory(slot, dir) &&
            (s_directory_slot < 0 || (int32_t)(dir->generation - s_generation) > 0)) {
            s_directory_slot = slot;
            s_generation = dir->generation;
            s_count = dir->count;
            memcpy(s_entries, dir->entries, s_count * sizeof(gb_rom_entry_t));
        }
    }
    free(dir);
    if (s_directory_slot >= 0) { s_catalog_trusted = true; return true; }
    rom_header_t header;
    if (esp_partition_read(s_partition, 0, &header, sizeof(header)) != ESP_OK ||
        memcmp(header.magic, "FGBR", 4) != 0 || header.version != 1 ||
        header.count == 0 || header.count > GB_STORAGE_MAX_ROMS) return true;
    size_t table_bytes = header.count * sizeof(gb_rom_entry_t);
    if (!valid_range(s_partition->size, sizeof(header), table_bytes) ||
        esp_partition_read(s_partition, sizeof(header), s_entries, table_bytes) != ESP_OK)
        return false;
    if (!entries_valid(s_entries, header.count, data_end())) return false;
    s_count = header.count;
    s_catalog_trusted = true;
    return true;
}

bool gb_storage_catalog_trusted(void) { return s_catalog_trusted; }

size_t gb_storage_count(void) { return s_count; }

const gb_rom_entry_t *gb_storage_entry(size_t index) {
    return index < s_count ? &s_entries[index] : NULL;
}

static bool read_selected(void *context, uint32_t offset, void *dst, size_t bytes) {
    const gb_rom_entry_t *entry = (const gb_rom_entry_t *)context;
    return s_partition && entry && dst &&
           valid_range(entry->size, offset, bytes) &&
           esp_partition_read(s_partition, entry->offset + offset, dst, bytes) == ESP_OK;
}

bool gb_storage_open(size_t index, gb_rom_source_t *out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!out || index >= s_count) return false;
    const gb_rom_entry_t *entry = &s_entries[index];
    uint8_t buffer[1024];
    uint8_t digest[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    int err = mbedtls_sha256_starts(&sha, 0);
    for (uint32_t offset = 0; err == 0 && offset < entry->size; offset += sizeof(buffer)) {
        size_t bytes = entry->size - offset;
        if (bytes > sizeof(buffer)) bytes = sizeof(buffer);
        if (!read_selected((void *)entry, offset, buffer, bytes)) { err = -1; break; }
        err = mbedtls_sha256_update(&sha, buffer, bytes);
    }
    if (err == 0) err = mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (err != 0 || memcmp(digest, entry->sha256, sizeof(digest)) != 0) return false;
    unmap_selected();
    const void *mapped = NULL;
    if (entry->size >= 32768) {
        esp_err_t map_err = esp_partition_mmap(s_partition, entry->offset, entry->size,
                                                ESP_PARTITION_MMAP_DATA,
                                                &mapped, &s_mapped_handle);
        if (map_err == ESP_OK) {
            s_mapped = true;
            ESP_LOGI(TAG, "ROM mapped directly from Flash: %lu bytes",
                     (unsigned long)entry->size);
        } else {
            ESP_LOGW(TAG, "ROM mapping failed (%s); using bank cache",
                     esp_err_to_name(map_err));
        }
    }
    s_selected = entry;
    *out = (gb_rom_source_t){.read = read_selected,
                             .context = (void *)s_selected,
                             .size = entry->size,
                             .mapped = (const uint8_t *)mapped};
    return true;
}

static uint32_t find_gap(uint32_t bytes) {
    uint32_t candidate = SECTOR_BYTES;
    while (candidate + bytes <= data_end()) {
        bool occupied = false;
        uint32_t next = candidate;
        for (size_t i = 0; i < s_count; i++) {
            const gb_rom_entry_t *entry = &s_entries[i];
            if (candidate < entry->offset + entry->size &&
                entry->offset < candidate + bytes) {
                occupied = true;
                if (entry->offset + entry->size > next) next = entry->offset + entry->size;
            }
        }
        if (!occupied) return candidate;
        candidate = next;
    }
    return 0;
}

size_t gb_storage_limit(void) { return USER_ROM_LIMIT; }

size_t gb_storage_used(void) {
    size_t used = 0;
    for (size_t i = 0; i < s_count; i++) used += s_entries[i].size;
    return used;
}

size_t gb_storage_free_bytes(void) {
    if (!s_partition) return 0;
    size_t capacity = data_end() - SECTOR_BYTES;
    size_t used = gb_storage_used();
    return used <= capacity ? capacity - used : 0;
}

size_t gb_storage_available(void) {
    if (!s_partition) return 0;
    size_t used = gb_storage_used();
    if (used >= USER_ROM_LIMIT) return 0;
    uint32_t best = 0;
    for (uint32_t from = SECTOR_BYTES; from < data_end();) {
        uint32_t to = data_end();
        for (size_t i = 0; i < s_count; i++) {
            const gb_rom_entry_t *entry = &s_entries[i];
            if (from >= entry->offset && from < entry->offset + entry->size) {
                from = entry->offset + entry->size;
                to = 0;
                break;
            }
            if (entry->offset > from && entry->offset < to) to = entry->offset;
        }
        if (!to) continue;
        if (to - from > best) best = to - from;
        from = to == data_end() ? data_end() : to;
    }
    size_t quota_left = USER_ROM_LIMIT - used;
    return best < quota_left ? best : quota_left;
}

static bool publish_directory(const gb_rom_entry_t *entries, size_t count) {
    directory_t *dir = calloc(1, sizeof(*dir));
    if (!dir) return false;
    memcpy(dir->magic, "FGB2", 4);
    dir->generation = s_generation + 1;
    dir->count = count;
    memcpy(dir->entries, entries, count * sizeof(*entries));
    bool success = mbedtls_sha256((const unsigned char *)dir,
                                  offsetof(directory_t, digest), dir->digest, 0) == 0;
    int slot = s_directory_slot == 0 ? 1 : 0;
    uint32_t offset = data_end() + (uint32_t)slot * SECTOR_BYTES;
    if (success) success = esp_partition_erase_range(s_partition, offset, SECTOR_BYTES) == ESP_OK;
    if (success) success = esp_partition_write(s_partition, offset, dir, sizeof(*dir)) == ESP_OK;
    directory_t *verify = malloc(sizeof(*verify));
    if (!verify) success = false;
    else {
        if (success) success = read_directory(slot, verify) &&
            verify->generation == dir->generation && verify->count == count;
        free(verify);
    }
    if (success) {
        memcpy(s_entries, entries, count * sizeof(*entries));
        s_count = count;
        s_generation = dir->generation;
        s_directory_slot = slot;
        s_catalog_trusted = true;
    }
    free(dir);
    return success;
}

static uint32_t declared_rom_size(uint8_t code) {
    if (code <= 7) return 32768u << code;
    switch (code) {
    case 0x52: return 72u * 16384;
    case 0x53: return 80u * 16384;
    case 0x54: return 96u * 16384;
    default: return 0;
    }
}

static bool supported_type(uint8_t type) {
    switch (type) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x05: case 0x06:
    case 0x0f: case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e:
        return true;
    default: return false;
    }
}

static bool valid_name(const char *name, size_t except) {
    if (!gb_name_valid(name)) return false;
    for (size_t i = 0; i < s_count; i++)
        if (i != except && !strcmp(name, s_entries[i].name)) return false;
    return true;
}

bool gb_storage_upload(const char *name, size_t bytes,
                       gb_storage_reader_t reader, void *context) {
    if (!s_partition || !reader || !valid_name(name, SIZE_MAX) || s_count >= GB_STORAGE_MAX_ROMS ||
        bytes < 32768 || bytes > USER_ROM_LIMIT || bytes % 16384 ||
        bytes > gb_storage_available()) return false;
    uint32_t offset = find_gap((uint32_t)bytes);
    if (!offset) return false;
    unmap_selected();
    if (esp_partition_erase_range(s_partition, offset, bytes) != ESP_OK) return false;
    uint8_t buffer[1024], header[0x150] = {0}, digest[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool ok = mbedtls_sha256_starts(&sha, 0) == 0;
    for (size_t written = 0; ok && written < bytes;) {
        size_t want = bytes - written;
        if (want > sizeof(buffer)) want = sizeof(buffer);
        int got = reader(context, buffer, want);
        if (got <= 0 || (size_t)got > want) { ok = false; break; }
        if (written < sizeof(header)) {
            size_t copy = sizeof(header) - written;
            if (copy > (size_t)got) copy = (size_t)got;
            memcpy(header + written, buffer, copy);
        }
        ok = esp_partition_write(s_partition, offset + written, buffer, got) == ESP_OK &&
             mbedtls_sha256_update(&sha, buffer, got) == 0;
        written += got;
    }
    if (ok) ok = mbedtls_sha256_finish(&sha, digest) == 0;
    mbedtls_sha256_free(&sha);
    if (!ok || header[0x143] == 0xc0 || !supported_type(header[0x147]) ||
        declared_rom_size(header[0x148]) != bytes) return false;
    gb_rom_entry_t updated[GB_STORAGE_MAX_ROMS];
    memcpy(updated, s_entries, s_count * sizeof(*updated));
    gb_rom_entry_t *entry = &updated[s_count];
    memset(entry, 0, sizeof(*entry));
    strcpy(entry->name, name);
    entry->offset = offset;
    entry->size = bytes;
    memcpy(entry->sha256, digest, sizeof(digest));
    return publish_directory(updated, s_count + 1);
}

bool gb_storage_delete(size_t index) {
    if (!s_partition || index >= s_count) return false;
    unmap_selected();
    gb_rom_entry_t updated[GB_STORAGE_MAX_ROMS];
    memcpy(updated, s_entries, s_count * sizeof(*updated));
    memmove(&updated[index], &updated[index + 1],
            (s_count - index - 1) * sizeof(*updated));
    return publish_directory(updated, s_count - 1);
}

bool gb_storage_rename(size_t index, const char *name) {
    if (!s_partition || index >= s_count || !valid_name(name, index)) return false;
    if (!strcmp(s_entries[index].name, name)) return true;
    unmap_selected();
    gb_rom_entry_t updated[GB_STORAGE_MAX_ROMS];
    memcpy(updated, s_entries, s_count * sizeof(*updated));
    memset(updated[index].name, 0, sizeof(updated[index].name));
    strcpy(updated[index].name, name);
    return publish_directory(updated, s_count);
}
