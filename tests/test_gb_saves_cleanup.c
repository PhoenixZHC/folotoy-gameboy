#define _POSIX_C_SOURCE 200809L
#define GB_SAVES_BASE_PATH "build/sv"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#define fsync(fd) (0)
#else
#define make_dir(path) mkdir(path, 0700)
#endif

#include "../main/gb_saves.c"

static gb_rom_entry_t live;
static bool trusted;
bool gb_storage_catalog_trusted(void) { return trusted; }
size_t gb_storage_count(void) { return 1; }
const gb_rom_entry_t *gb_storage_entry(size_t index) { return index == 0 ? &live : NULL; }
size_t gb_port_sram_size(const gb_session_t *session) { (void)session; return 0; }
bool gb_port_has_battery(const gb_session_t *session) { (void)session; return false; }
bool gb_port_needs_save(const gb_session_t *session) { (void)session; return false; }
void gb_port_mark_saved(gb_session_t *session) { (void)session; }
bool gb_port_read_sram(const gb_session_t *session, size_t offset, void *dst, size_t bytes) {
    (void)session; (void)offset; (void)dst; (void)bytes; return false;
}
bool gb_port_write_sram(gb_session_t *session, size_t offset, const void *src, size_t bytes) {
    (void)session; (void)offset; (void)src; (void)bytes; return false;
}
bool gb_port_export_rtc(gb_session_t *session, uint8_t dst[10]) {
    (void)session; (void)dst; return false;
}
bool gb_port_import_rtc(gb_session_t *session, const uint8_t src[10]) {
    (void)session; (void)src; return false;
}

static void write_file(const char *path) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputc('x', file) == 'x');
    assert(fclose(file) == 0);
}

static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

int main(void) {
    assert(make_dir("build") == 0 || errno == EEXIST);
    assert(make_dir(GB_SAVES_BASE_PATH) == 0 || errno == EEXIST);
    uint8_t orphan[32] = {0};
    memset(orphan, 0x22, sizeof(orphan));
    memset(live.sha256, 0x11, sizeof(live.sha256));
    char live_a[32], live_b[32], orphan_a[32], orphan_b[32];
    save_path(live.sha256, 'a', live_a);
    save_path(live.sha256, 'b', live_b);
    save_path(orphan, 'a', orphan_a);
    save_path(orphan, 'b', orphan_b);
    write_file(live_a); write_file(live_b);
    write_file(orphan_a); write_file(orphan_b);
    write_file(GB_SAVES_BASE_PATH "/unrelated.txt");
    assert(gb_saves_init());
    assert(!gb_saves_prune_orphans());
    assert(file_exists(orphan_a) && file_exists(orphan_b));
    trusted = true;
    assert(gb_saves_prune_orphans());
    assert(file_exists(live_a) && file_exists(live_b));
    assert(!file_exists(orphan_a) && !file_exists(orphan_b));
    assert(file_exists(GB_SAVES_BASE_PATH "/unrelated.txt"));
    assert(gb_saves_delete(live.sha256));
    assert(gb_saves_delete(live.sha256));
    assert(!file_exists(live_a) && !file_exists(live_b));
    assert(remove(GB_SAVES_BASE_PATH "/unrelated.txt") == 0);
    assert(rmdir(GB_SAVES_BASE_PATH) == 0);
    return 0;
}
