#include "gb_saves.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "gb_storage.h"

#ifndef GB_SAVES_BASE_PATH
#define GB_SAVES_BASE_PATH "/saves"
#endif
#define GB_SAVES_PREFIX GB_SAVES_BASE_PATH "/"
_Static_assert(sizeof(GB_SAVES_PREFIX) + 18 <= 32, "save path buffer too small");

static bool s_mounted;
static const char *TAG = "gb_saves";

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t sram_bytes;
    uint8_t rom_hash[32];
    uint32_t crc32;
} save_header_t;

_Static_assert(sizeof(save_header_t) == 52, "save header size");

static uint32_t crc_update(uint32_t crc, const void *data, size_t bytes) {
    const uint8_t *p = data;
    for (size_t i = 0; i < bytes; i++) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static void save_path(const uint8_t hash[32], char copy, char path[32]) {
    static const char hex[] = "0123456789abcdef";
    const size_t prefix = sizeof(GB_SAVES_PREFIX) - 1;
    memcpy(path, GB_SAVES_PREFIX, prefix);
    for (int i = 0; i < 8; i++) {
        path[prefix + 2 * i] = hex[hash[i] >> 4];
        path[prefix + 2 * i + 1] = hex[hash[i] & 15];
    }
    path[prefix + 16] = '.';
    path[prefix + 17] = copy;
    path[prefix + 18] = 0;
}

bool gb_saves_init(void) {
    const esp_vfs_spiffs_conf_t config = {
        .base_path = GB_SAVES_BASE_PATH, .partition_label = "saves",
        .max_files = 4, .format_if_mount_failed = false,
    };
    s_mounted = esp_vfs_spiffs_register(&config) == ESP_OK;
    return s_mounted;
}

bool gb_saves_ready(void) { return s_mounted; }

static bool remove_copy(const char *path) {
    if (remove(path) == 0 || errno == ENOENT) return true;
    ESP_LOGE(TAG, "save removal failed: %s errno=%d", path, errno);
    return false;
}

bool gb_saves_delete(const uint8_t hash[32]) {
    if (!s_mounted || !hash) return false;
    char a[32], b[32];
    save_path(hash, 'a', a);
    save_path(hash, 'b', b);
    bool first = remove_copy(a);
    bool second = remove_copy(b);
    return first && second;
}

static bool live_save_name(const char *filename) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < gb_storage_count(); i++) {
        const gb_rom_entry_t *entry = gb_storage_entry(i);
        bool match = true;
        for (int byte = 0; byte < 8; byte++) {
            if (filename[byte * 2] != hex[entry->sha256[byte] >> 4] ||
                filename[byte * 2 + 1] != hex[entry->sha256[byte] & 15]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool gb_saves_prune_orphans(void) {
    if (!s_mounted || !gb_storage_catalog_trusted()) return false;
    DIR *dir = opendir(GB_SAVES_BASE_PATH);
    if (!dir) return false;
    bool okay = true;
    struct dirent *item;
    while (true) {
        errno = 0;
        item = readdir(dir);
        if (!item) {
            if (errno) okay = false;
            break;
        }
        const char *name = item->d_name;
        if (strlen(name) != 18 || name[16] != '.' ||
            (name[17] != 'a' && name[17] != 'b')) continue;
        bool valid = true;
        for (int i = 0; i < 16; i++) {
            char c = name[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) valid = false;
        }
        if (!valid || live_save_name(name)) continue;
        char path[32];
        const size_t prefix = sizeof(GB_SAVES_PREFIX) - 1;
        memcpy(path, GB_SAVES_PREFIX, prefix);
        memcpy(path + prefix, name, 19);
        if (!remove_copy(path)) okay = false;
        else rewinddir(dir);
    }
    if (closedir(dir) != 0) okay = false;
    return okay;
}

// 仅读取并校验文件，不改动模拟器。CRC 覆盖头部前 48 字节及全部负载。
static bool check_copy(const char *path, const uint8_t hash[32],
                       uint32_t expected_sram, save_header_t *out) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    save_header_t h;
    bool valid = fread(&h, 1, sizeof(h), file) == sizeof(h) &&
                 memcmp(h.magic, "FGBS", 4) == 0 && h.version == 1 &&
                 h.reserved == 0 && h.sram_bytes == expected_sram &&
                 memcmp(h.rom_hash, hash, 32) == 0;
    if (valid) {
        uint32_t crc = crc_update(0xffffffffu, &h, sizeof(h) - sizeof(h.crc32));
        uint8_t chunk[512];
        uint32_t remaining = expected_sram + 10;
        while (valid && remaining) {
            size_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
            valid = fread(chunk, 1, n, file) == n;
            if (valid) crc = crc_update(crc, chunk, n);
            remaining -= n;
        }
        valid = valid && fgetc(file) == EOF && (crc ^ 0xffffffffu) == h.crc32;
    }
    valid = fclose(file) == 0 && valid;
    if (valid && out) *out = h;
    return valid;
}

static bool exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

gb_save_result_t gb_saves_load(gb_session_t *session, const uint8_t hash[32]) {
    if (!session || !hash) return GB_SAVE_ERROR;
    if (!gb_port_has_battery(session)) return GB_SAVE_EMPTY;
    uint32_t sram = gb_port_sram_size(session);
    char a[32], b[32];
    save_path(hash, 'a', a);
    save_path(hash, 'b', b);
    save_header_t ha, hb;
    bool va = check_copy(a, hash, sram, &ha);
    bool vb = check_copy(b, hash, sram, &hb);
    if (!va && !vb) return exists(a) || exists(b) ? GB_SAVE_ERROR : GB_SAVE_EMPTY;
    const char *path = va && (!vb || (int32_t)(ha.sequence - hb.sequence) > 0) ? a : b;
    FILE *file = fopen(path, "rb");
    if (!file) return GB_SAVE_ERROR;
    bool okay = fseek(file, sizeof(save_header_t), SEEK_SET) == 0;
    uint8_t chunk[512];
    for (uint32_t offset = 0; okay && offset < sram; offset += sizeof(chunk)) {
        size_t n = sram - offset;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        okay = fread(chunk, 1, n, file) == n &&
               gb_port_write_sram(session, offset, chunk, n);
    }
    uint8_t rtc[10];
    okay = okay && fread(rtc, 1, sizeof(rtc), file) == sizeof(rtc) &&
           gb_port_import_rtc(session, rtc);
    okay = fclose(file) == 0 && okay;
    if (okay) gb_port_mark_saved(session);
    return okay ? GB_SAVE_LOADED : GB_SAVE_ERROR;
}

bool gb_saves_write(gb_session_t *session, const uint8_t hash[32]) {
    if (!session || !hash) return false;
    if (!gb_port_has_battery(session)) return true;
    if (!gb_port_needs_save(session)) return true;
    uint32_t sram = gb_port_sram_size(session);
    char a[32], b[32];
    save_path(hash, 'a', a);
    save_path(hash, 'b', b);
    save_header_t ha, hb;
    bool va = check_copy(a, hash, sram, &ha);
    bool vb = check_copy(b, hash, sram, &hb);
    // 首次保存短写时可以写另一空槽重试，保留短写副本供诊断。
    if (!va && !vb && exists(a) && exists(b)) return false;
    uint32_t latest = va && (!vb || (int32_t)(ha.sequence - hb.sequence) > 0)
                          ? ha.sequence : vb ? hb.sequence : 0;
    const char *target = !va && !vb ? (exists(a) ? b : a) : !va ? a : !vb ? b :
                         (int32_t)(ha.sequence - hb.sequence) > 0 ? b : a;
    save_header_t h = {
        .magic = {'F', 'G', 'B', 'S'}, .version = 1,
        .sequence = latest + 1, .sram_bytes = sram,
    };
    memcpy(h.rom_hash, hash, 32);
    uint32_t crc = crc_update(0xffffffffu, &h, sizeof(h) - sizeof(h.crc32));
    uint8_t chunk[512];
    for (uint32_t offset = 0; offset < sram; offset += sizeof(chunk)) {
        size_t n = sram - offset;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        if (!gb_port_read_sram(session, offset, chunk, n)) return false;
        crc = crc_update(crc, chunk, n);
    }
    uint8_t rtc[10];
    if (!gb_port_export_rtc(session, rtc)) return false;
    h.crc32 = crc_update(crc, rtc, sizeof(rtc)) ^ 0xffffffffu;

    FILE *file = fopen(target, "wb");
    if (!file) return false;
    bool okay = fwrite(&h, 1, sizeof(h), file) == sizeof(h);
    for (uint32_t offset = 0; okay && offset < sram; offset += sizeof(chunk)) {
        size_t n = sram - offset;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        okay = gb_port_read_sram(session, offset, chunk, n) &&
               fwrite(chunk, 1, n, file) == n;
    }
    okay = okay && fwrite(rtc, 1, sizeof(rtc), file) == sizeof(rtc);
    okay = okay && fflush(file) == 0;
    if (okay) okay = fsync(fileno(file)) == 0;
    okay = fclose(file) == 0 && okay;
    okay = okay && check_copy(target, hash, sram, NULL);
    if (okay) gb_port_mark_saved(session);
    return okay;
}
