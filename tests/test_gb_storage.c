#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
static int fail_alloc;
static bool track_commit_allocations, directory_written;
static unsigned late_allocations;
static void *storage_malloc(size_t size) {
    if (track_commit_allocations && directory_written) {
        late_allocations++;
        return NULL;
    }
    return malloc(size);
}
static void *storage_calloc(size_t count, size_t size) {
    if (track_commit_allocations && directory_written) {
        late_allocations++;
        return NULL;
    }
    if (fail_alloc) return NULL;
    return calloc(count, size);
}
#define calloc storage_calloc
#define malloc storage_malloc
#include "../main/gb_storage.c"
#undef calloc
#undef malloc

static unsigned char flash[4 * 1024 * 1024];
static const esp_partition_t partition = { sizeof(flash) };
static int fail_directory, fail_readback, erase_calls;
static int fail_readback_slot = -1;
const esp_partition_t *esp_partition_find_first(int type, esp_partition_subtype_t sub, const char *name) { (void)type; (void)sub; (void)name; return &partition; }
esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *dst, size_t n) {
    assert(off <= p->size && n <= p->size - off);
    if (fail_readback && off >= data_end() &&
        (fail_readback_slot < 0 || off == data_end() + (size_t)fail_readback_slot * SECTOR_BYTES)) return -1;
    memcpy(dst, flash + off, n); return 0;
}
esp_err_t esp_partition_write(const esp_partition_t *p, size_t off, const void *src, size_t n) {
    assert(off <= p->size && n <= p->size - off);
    if (fail_directory && off >= data_end()) { memcpy(flash + off, src, n / 2); return -1; }
    if (off >= data_end()) directory_written = true;
    memcpy(flash + off, src, n); return 0;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t off, size_t n) { assert(off % 4096 == 0 && n % 4096 == 0 && off + n <= p->size); erase_calls++; memset(flash + off, 255, n); return 0; }
esp_err_t esp_partition_mmap(const esp_partition_t *p, size_t off, size_t n, int mode, const void **out, esp_partition_mmap_handle_t *h) { (void)p; (void)n; (void)mode; *out = flash + off; *h = 1; return 0; }
void esp_partition_munmap(esp_partition_mmap_handle_t h) { (void)h; }

typedef struct { size_t at, stop; unsigned char code, cgb; } input_t;
static int receive(void *context, void *dst, size_t n) {
    input_t *in = context;
    if (in->at >= in->stop) return -1;
    if (n > in->stop - in->at) n = in->stop - in->at;
    /* Small chunks also split the header across reads. */
    if (n > 79) n = 79;
    memset(dst, 0, n);
    for (size_t i = 0; i < n; i++) {
        if (in->at + i == 0x148) ((unsigned char *)dst)[i] = in->code;
        if (in->at + i == 0x143) ((unsigned char *)dst)[i] = in->cgb;
    }
    in->at += n; return (int)n;
}
static bool upload(const char *name, unsigned char code, size_t stop, unsigned char cgb) {
    input_t in = {0, stop, code, cgb};
    return gb_storage_upload(name, 32768u << code, receive, &in);
}
static void reset(void) { memset(flash, 255, sizeof(flash)); fail_directory = fail_readback = fail_alloc = 0; fail_readback_slot = -1; erase_calls = 0; assert(gb_storage_init()); }
static void reopen_one(void) { assert(gb_storage_init()); assert(gb_storage_count() == 1); gb_rom_source_t source; assert(gb_storage_open(0, &source)); }
static void preloaded_game_lifecycle(void) {
    // Factory FGBR directory follows the same writable catalog path as uploads.
    for (int rename_first = 0; rename_first < 2; rename_first++) {
        reset();
        rom_header_t header = {{'F', 'G', 'B', 'R'}, 1, 1};
        gb_rom_entry_t entry = {.name = "preloaded", .offset = 4096, .size = 32768};
        memset(flash + entry.offset, 0, entry.size);
        mbedtls_sha256(flash + entry.offset, entry.size, entry.sha256, 0);
        memcpy(flash, &header, sizeof(header));
        memcpy(flash + sizeof(header), &entry, sizeof(entry));
        reopen_one();
        assert(s_directory_slot == -1 && gb_storage_catalog_trusted());
        if (rename_first) {
            assert(gb_storage_rename(0, "renamed-preload"));
            reopen_one();
            assert(!strcmp(gb_storage_entry(0)->name, "renamed-preload"));
        }
        assert(gb_storage_delete(0));
        for (unsigned reboot = 0; reboot < 3; reboot++) {
            assert(gb_storage_init());
            assert(gb_storage_count() == 0 && gb_storage_catalog_trusted());
        }
        // The old factory directory remains on Flash but cannot resurrect the game.
        assert(!memcmp(flash, "FGBR", 4));
        assert(gb_storage_available() == gb_storage_limit());
        assert(upload("replacement", 0, SIZE_MAX, 0));
        reopen_one();
        assert(gb_storage_entry(0)->offset == entry.offset);
    }
}
static void commit_failure_lifecycle(void) {
    reset();
    assert(upload("original", 0, SIZE_MAX, 0));
    fail_alloc = 1;
    int before = erase_calls;
    assert(!gb_storage_delete(0));
    assert(!gb_storage_rename(0, "renamed"));
    assert(erase_calls == before && !gb_storage_needs_reload());
    assert(gb_storage_catalog_trusted());
    fail_alloc = 0;
    reopen_one();
    assert(!strcmp(gb_storage_entry(0)->name, "original"));
    directory_written = false;
    track_commit_allocations = true;
    assert(gb_storage_rename(0, "renamed"));
    assert(directory_written && late_allocations == 0);
    track_commit_allocations = false;

    // The write commits, but readback cannot confirm it. RAM must not be used
    // for further writes; reload must not silently fall back to the older slot.
    fail_readback = 1;
    fail_readback_slot = s_directory_slot == 0 ? 1 : 0;
    assert(!gb_storage_delete(0));
    assert(gb_storage_needs_reload() && !gb_storage_catalog_trusted());
    before = erase_calls;
    assert(!upload("unsafe-retry", 0, SIZE_MAX, 0));
    assert(!gb_storage_delete(0));
    assert(!gb_storage_rename(0, "unsafe-name"));
    gb_rom_source_t source;
    assert(!gb_storage_open(0, &source));
    assert(erase_calls == before);
    assert(!gb_storage_init() && gb_storage_needs_reload());
    assert(!upload("still-unsafe", 0, SIZE_MAX, 0));
    assert(erase_calls == before);
    fail_readback = 0;
    assert(gb_storage_init());
    assert(gb_storage_count() == 0 && gb_storage_catalog_trusted());
    assert(!gb_storage_needs_reload());
    assert(upload("safe-replacement", 0, SIZE_MAX, 0));
    reopen_one();

    // A committed upload must not be overwritten through the old RAM gap map.
    for (unsigned operation = 0; operation < 2; operation++) {
        reset();
        assert(upload("original", 0, SIZE_MAX, 0));
        fail_readback = 1;
        fail_readback_slot = s_directory_slot == 0 ? 1 : 0;
        if (operation == 0) assert(!upload("committed", 0, SIZE_MAX, 0));
        else assert(!gb_storage_rename(0, "committed"));
        assert(gb_storage_needs_reload() && !gb_storage_catalog_trusted());
        before = erase_calls;
        assert(!upload("must-not-overwrite", 0, SIZE_MAX, 0));
        assert(!gb_storage_delete(0));
        assert(erase_calls == before);
        fail_readback = 0;
        assert(gb_storage_init());
        size_t committed_index = operation == 0 ? 1 : 0;
        assert(gb_storage_count() == committed_index + 1);
        assert(!strcmp(gb_storage_entry(committed_index)->name, "committed"));
        assert(gb_storage_open(committed_index, &source));
    }
}

static void stale_catalog_tokens(void) {
    reset();
    assert(upload("first", 0, SIZE_MAX, 0));
    assert(upload("second", 0, SIZE_MAX, 0));
    char first[GB_STORAGE_TOKEN_BYTES], second[GB_STORAGE_TOKEN_BYTES];
    size_t index;
    assert(gb_storage_token(0, first) && gb_storage_token(1, second));
    assert(memcmp(gb_storage_entry(0)->sha256, gb_storage_entry(1)->sha256, 32) == 0);
    assert(strcmp(first, second));
    assert(gb_storage_resolve_token(second, &index) && index == 1);
    assert(gb_storage_init());
    assert(gb_storage_resolve_token(second, &index) && index == 1);
    assert(gb_storage_delete(0));
    assert(!gb_storage_resolve_token(first, &index));
    assert(!gb_storage_resolve_token(second, &index));
    assert(gb_storage_token(0, second));
    assert(gb_storage_delete(0));
    assert(upload("second", 0, SIZE_MAX, 0));
    assert(!gb_storage_resolve_token(second, &index));
    assert(gb_storage_token(0, second));
    assert(gb_storage_rename(0, "changed"));
    assert(!gb_storage_resolve_token(second, &index));
}

int main(void) {
    commit_failure_lifecycle();
    stale_catalog_tokens();
    preloaded_game_lifecycle();
    reset();
    assert(upload("original", 0, SIZE_MAX, 0));
    size_t free_before = gb_storage_free_bytes();
    int untouched = erase_calls;
    assert(!upload("original", 0, SIZE_MAX, 0));
    assert(!upload("../invalid", 0, SIZE_MAX, 0));
    assert(erase_calls == untouched);
    assert(!upload("interrupted", 0, 1000, 0));
    reopen_one(); assert(gb_storage_free_bytes() == free_before);
    assert(!upload("color-only", 0, SIZE_MAX, 0xc0)); reopen_one();
    input_t bad = {0, SIZE_MAX, 1, 0};
    assert(!gb_storage_upload("wrong-size", 32768, receive, &bad)); reopen_one();
    fail_directory = 1;
    assert(!upload("torn-directory", 0, SIZE_MAX, 0)); reopen_one();
    assert(!gb_storage_delete(0)); reopen_one();
    fail_directory = 0;
    assert(upload("retry", 0, SIZE_MAX, 0));
    assert(gb_storage_init()); assert(gb_storage_count() == 2);
    assert(gb_storage_delete(1)); reopen_one();
    assert(gb_storage_rename(0, "中文游戏"));
    assert(gb_storage_init()); assert(!strcmp(gb_storage_entry(0)->name, "中文游戏"));
    assert(gb_storage_delete(0)); assert(gb_storage_init()); assert(gb_storage_count() == 0);
    assert(gb_storage_free_bytes() == free_before + 32768);
    assert(upload("reused", 0, SIZE_MAX, 0));
    flash[gb_storage_entry(0)->offset + 1000] ^= 1;
    gb_rom_source_t source; assert(!gb_storage_open(0, &source));
    reset();
    assert(gb_storage_limit() == 4 * 1024 * 1024 - 16 * 1024);
    assert(gb_storage_available() == gb_storage_limit());
    assert(upload("two-MiB", 6, SIZE_MAX, 0));
    assert(upload("one-MiB", 5, SIZE_MAX, 0));
    assert(gb_storage_available() == gb_storage_limit() - 3 * 1024 * 1024);
    assert(upload("half-MiB", 4, SIZE_MAX, 0));
    assert(upload("quarter-MiB", 3, SIZE_MAX, 0));
    assert(upload("eighth-MiB", 2, SIZE_MAX, 0));
    assert(upload("sixty-four-KiB", 1, SIZE_MAX, 0));
    assert(upload("thirty-two-KiB", 0, SIZE_MAX, 0));
    assert(gb_storage_used() == 4 * 1024 * 1024 - 32 * 1024);
    assert(gb_storage_available() == 0);
    int before = erase_calls;
    assert(!upload("over-total-capacity", 0, SIZE_MAX, 0));
    assert(erase_calls == before);
    assert(gb_storage_init()); assert(gb_storage_count() == 7);
    assert(gb_storage_delete(5));
    assert(gb_storage_available() == 64 * 1024);
    assert(upload("replacement", 1, SIZE_MAX, 0));
    assert(gb_storage_init()); assert(gb_storage_count() == 7);
    puts("Storage faults: PASS (RAM Flash model, not physical power-loss validation)");
    return 0;
}
