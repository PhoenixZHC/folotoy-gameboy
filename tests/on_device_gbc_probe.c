// Temporary opt-in hardware probe; never part of the normal firmware build.
#include <string.h>
#include "bsp_audio.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"
#include "gb_port.h"
#include "gb_storage.h"
#include "gb_audio.h"
#include "gb_web.h"

static const uint8_t s_rom[32768] = {
    [0x143] = 0xc0, // CGB-only
    [0x147] = 0x1b, // MBC5 + RAM + battery
    [0x148] = 0x00, // 32 KiB ROM
    [0x149] = 0x03, // 32 KiB SRAM (worst supported case)
};
static const uint8_t s_gb_rom[32768] = {
    [0x147] = 0x00,
    [0x148] = 0x00,
};

static bool read_rom(void *context, uint32_t offset, void *dst, size_t bytes) {
    (void)context;
    if (offset > sizeof(s_rom) || bytes > sizeof(s_rom) - offset) return false;
    memcpy(dst, s_rom + offset, bytes);
    return true;
}

typedef struct { size_t offset; } reader_state_t;
static int upload_read(void *context, void *dst, size_t bytes) {
    reader_state_t *state = (reader_state_t *)context;
    if (bytes > sizeof(s_gb_rom) - state->offset) return -1;
    memcpy(dst, s_gb_rom + state->offset, bytes);
    state->offset += bytes;
    return (int)bytes;
}

void gbc_hardware_probe(void) {
    const char *tag = "gbc_probe";
    gb_rom_source_t rom = {.read = read_rom, .size = sizeof(s_rom), .mapped = s_rom};
    gb_session_t *session = NULL;
    ESP_LOGI(tag, "before: heap=%lu largest=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (!gb_port_create(&rom, &session)) {
        ESP_LOGE(tag, "CGB 32K-SRAM allocation FAILED");
        goto web_only;
    }
    ESP_LOGI(tag, "core ready: heap=%lu largest=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    esp_err_t e = bsp_audio_init_playback();
    if (e == ESP_OK) e = bsp_audio_set_format(16000, 16, 1);
    ESP_LOGI(tag, "audio=%s heap=%lu largest=%lu", esp_err_to_name(e),
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    bool worker_ready = e == ESP_OK && gb_audio_start(40);
    ESP_LOGI(tag, "worker=%s heap=%lu largest=%lu", worker_ready ? "ready" : "FAILED",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    for (int i = 0; i < 20; i++) {
        if (!gb_port_step_frame(session, true)) {
            ESP_LOGE(tag, "frame %d failed", i);
            break;
        }
        if (worker_ready) {
            int16_t samples[GB_AUDIO_SAMPLES];
            size_t count = gb_port_audio_frame(session, samples, GB_AUDIO_SAMPLES);
            if (count == GB_AUDIO_SAMPLES) gb_audio_submit(samples, count);
        }
    }
    ESP_LOGI(tag, "frames done: heap=%lu largest=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    gb_port_destroy(session);
    if (worker_ready) ESP_LOGI(tag, "worker stop=%s", gb_audio_stop() ? "OK" : "FAILED");
web_only:;
    bool web_ready = gb_web_start();
    ESP_LOGI(tag, "web=%s heap=%lu largest=%lu", web_ready ? "ready" : "FAILED",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (web_ready) gb_web_stop();
    size_t old_count = gb_storage_count();
    uint8_t old_hashes[GB_STORAGE_MAX_ROMS][32] = {{0}};
    for (size_t i = 0; i < old_count; i++) {
        const gb_rom_entry_t *entry = gb_storage_entry(i);
        memcpy(old_hashes[i], entry->sha256, 32);
    }
    reader_state_t reader = {0};
    bool uploaded = gb_storage_upload("codex_probe", sizeof(s_gb_rom), upload_read, &reader);
    bool opened = false, deleted = false, preserved = true;
    if (uploaded) {
        gb_rom_source_t source;
        opened = gb_storage_open(old_count, &source);
        deleted = gb_storage_delete(old_count);
    }
    if (gb_storage_count() != old_count) preserved = false;
    for (size_t i = 0; i < old_count; i++) {
        const gb_rom_entry_t *entry = gb_storage_entry(i);
        if (!entry || memcmp(entry->sha256, old_hashes[i], 32)) preserved = false;
    }
    ESP_LOGI(tag, "storage upload=%d opened=%d delete=%d previous=%d count=%u",
             uploaded, opened, deleted, preserved, (unsigned)gb_storage_count());
}
