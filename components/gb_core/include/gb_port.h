#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gb_session gb_session_t;
typedef bool (*gb_rom_read_fn)(void *context, uint32_t offset, void *dst, size_t bytes);
typedef struct {
    gb_rom_read_fn read;
    void *context;
    uint32_t size;
    const uint8_t *mapped;
} gb_rom_source_t;

typedef struct {
    bool right, left, up, down;
    bool a, b, select, start;
} gb_port_keys_t;

bool gb_port_create(const gb_rom_source_t *rom, gb_session_t **out);
void gb_port_set_keys(gb_session_t *session, gb_port_keys_t keys);
bool gb_port_step_frame(gb_session_t *session, bool render);
const uint8_t *gb_port_framebuffer(const gb_session_t *session);
bool gb_port_is_cgb(const gb_session_t *session);
const uint8_t *gb_port_color_framebuffer(const gb_session_t *session);
const uint8_t *gb_port_bg_palette(const gb_session_t *session);
const uint8_t *gb_port_obj_palette(const gb_session_t *session);
// Advances APU by 267 mono samples at 16 kHz; may run more than once per frame.
size_t gb_port_audio_frame(gb_session_t *session, int16_t *out, size_t capacity);
size_t gb_port_sram_size(const gb_session_t *session);
bool gb_port_has_battery(const gb_session_t *session);
bool gb_port_needs_save(const gb_session_t *session);
void gb_port_mark_saved(gb_session_t *session);
bool gb_port_export_sram(const gb_session_t *session, void *dst, size_t bytes);
bool gb_port_import_sram(gb_session_t *session, const void *src, size_t bytes);
bool gb_port_read_sram(const gb_session_t *session, size_t offset, void *dst, size_t bytes);
bool gb_port_write_sram(gb_session_t *session, size_t offset, const void *src, size_t bytes);
bool gb_port_export_rtc(gb_session_t *session, uint8_t dst[10]);
bool gb_port_import_rtc(gb_session_t *session, const uint8_t src[10]);
void gb_port_destroy(gb_session_t *session);

#ifdef __cplusplus
}
#endif
