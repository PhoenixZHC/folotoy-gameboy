#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#include "../main/gb_storage.c"

static unsigned char flash[4 * 1024 * 1024];
static const esp_partition_t partition = { sizeof(flash) };
static int fail_directory, erase_calls;
const esp_partition_t *esp_partition_find_first(int type, esp_partition_subtype_t sub, const char *name) { (void)type; (void)sub; (void)name; return &partition; }
esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *dst, size_t n) { assert(off <= p->size && n <= p->size - off); memcpy(dst, flash + off, n); return 0; }
esp_err_t esp_partition_write(const esp_partition_t *p, size_t off, const void *src, size_t n) {
    assert(off <= p->size && n <= p->size - off);
    if (fail_directory && off >= data_end()) { memcpy(flash + off, src, n / 2); return -1; }
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
static void reset(void) { memset(flash, 255, sizeof(flash)); fail_directory = 0; erase_calls = 0; assert(gb_storage_init()); }
static void reopen_one(void) { assert(gb_storage_init()); assert(gb_storage_count() == 1); gb_rom_source_t source; assert(gb_storage_open(0, &source)); }
int main(void) {
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
    assert(upload("two-MiB", 6, SIZE_MAX, 0));
    assert(upload("one-MiB", 5, SIZE_MAX, 0));
    int before = erase_calls;
    assert(!upload("over-total-quota", 0, SIZE_MAX, 0));
    assert(erase_calls == before);
    assert(gb_storage_init()); assert(gb_storage_count() == 2);
    assert(gb_storage_available() == 0);
    assert(gb_storage_delete(1));
    assert(gb_storage_available() == 1024 * 1024);
    assert(upload("replacement", 5, SIZE_MAX, 0));
    puts("Storage faults: PASS (RAM Flash model, not physical power-loss validation)");
    return 0;
}
