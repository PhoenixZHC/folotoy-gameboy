#include "gb_port.h"

#include <new>
#include <stdlib.h>
#include <string.h>
#include "gb/gb_emulator.h"
extern "C" {
#include "minigb_apu.h"
}

struct gb_session {
    GBEmulator *core = nullptr;
    gb_rom_source_t rom = {};
    uint8_t *bank0 = nullptr;
    uint8_t *cache = nullptr;
    int cache_map = -1;
    uint8_t *cgb_vram = nullptr;
    uint8_t *cgb_wram = nullptr;
    bool battery = false;
    bool rtc = false;
    minigb_apu_ctx apu = {};
    audio_sample_t stereo[AUDIO_SAMPLES_TOTAL] = {};
};

static void apu_write(void *context, uint16_t addr, uint8_t value) {
    minigb_apu_audio_write(static_cast<minigb_apu_ctx *>(context), addr, value);
}

static uint8_t apu_read(void *context, uint16_t addr) {
    return minigb_apu_audio_read(static_cast<minigb_apu_ctx *>(context), addr);
}

static bool supported_cart(uint8_t type) {
    switch (type) {
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x05: case 0x06:
    case 0x0f: case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e:
        return true;
    default:
        return false;
    }
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

extern "C" bool gb_port_create(const gb_rom_source_t *rom, gb_session_t **out) {
    if (out) *out = nullptr;
    if (!out || !rom || !rom->read || rom->size < 32768 || rom->size > 4194304 ||
        rom->size % 16384 != 0) return false;
    uint8_t header[0x150];
    if (!rom->read(rom->context, 0, header, sizeof(header))) return false;
    if (header[0x143] == 0xc0 || !supported_cart(header[0x147]) ||
        declared_rom_size(header[0x148]) != rom->size) return false;

    gb_session *session = new (std::nothrow) gb_session;
    if (!session) return false;
    session->rom = *rom;
    switch (header[0x147]) {
    case 0x03: case 0x06: case 0x0f: case 0x10: case 0x13:
    case 0x1b: case 0x1e:
        session->battery = true;
        break;
    default:
        break;
    }
    session->rtc = header[0x147] == 0x0f || header[0x147] == 0x10;
    session->core = new (std::nothrow) GBEmulator;
    if (!session->core) {
        gb_port_destroy(session);
        return false;
    }
#ifdef GB_ENABLE_CGB
    if (header[0x143] & 0x80) {
        session->cgb_vram = static_cast<uint8_t *>(malloc(0x2000));
        session->cgb_wram = static_cast<uint8_t *>(malloc(0x6000));
        if (!session->cgb_vram || !session->cgb_wram) {
            gb_port_destroy(session);
            return false;
        }
        session->core->enableCgbMode(session->cgb_vram, session->cgb_wram);
    }
#endif
    if (!rom->mapped) {
        session->bank0 = static_cast<uint8_t *>(malloc(16384));
        session->cache = static_cast<uint8_t *>(malloc(16384));
        if (!session->bank0 || !session->cache ||
            !rom->read(rom->context, 0, session->bank0, 16384) ||
            !session->core->romFile_.attachRom(rom->read, rom->context, rom->size)) {
            gb_port_destroy(session);
            return false;
        }
        session->core->setupBankCache(session->bank0, session->cache, 1,
                                      &session->cache_map);
    }
    if (!session->core->loadRom("flash", rom->mapped, rom->size) ||
        !session->core->init()) {
        gb_port_destroy(session);
        return false;
    }
    minigb_apu_audio_init(&session->apu);
    session->core->soundContext_ = &session->apu;
    session->core->soundWrite_ = apu_write;
    session->core->soundRead_ = apu_read;
    *out = session;
    return true;
}

extern "C" void gb_port_set_keys(gb_session_t *session, gb_port_keys_t keys) {
    if (!session || !session->core) return;
    uint8_t mask = 0;
    if (keys.right) mask |= INPUT_RIGHT;
    if (keys.left) mask |= INPUT_LEFT;
    if (keys.up) mask |= INPUT_UP;
    if (keys.down) mask |= INPUT_DOWN;
    if (keys.a) mask |= INPUT_A;
    if (keys.b) mask |= INPUT_B;
    if (keys.start) mask |= INPUT_START;
    if (keys.select) mask |= INPUT_SELECT;
    session->core->setInput(mask);
}

extern "C" bool gb_port_step_frame(gb_session_t *session, bool render) {
    if (!session || !session->core || session->core->romIoFailed_) return false;
    session->core->renderThisFrame_ = render;
    session->core->runFrame();
    session->core->frameCount++;
    return !session->core->romIoFailed_;
}

extern "C" const uint8_t *gb_port_framebuffer(const gb_session_t *session) {
    return session && session->core ? session->core->framebuf : nullptr;
}

extern "C" bool gb_port_is_cgb(const gb_session_t *session) {
    return session && session->core && session->core->isCgb_;
}

extern "C" const uint8_t *gb_port_color_framebuffer(const gb_session_t *session) {
    return gb_port_is_cgb(session) ? session->core->colorFramebuf_ : nullptr;
}

extern "C" const uint8_t *gb_port_bg_palette(const gb_session_t *session) {
    return gb_port_is_cgb(session) ? session->core->bgPalette_ : nullptr;
}

extern "C" const uint8_t *gb_port_obj_palette(const gb_session_t *session) {
    return gb_port_is_cgb(session) ? session->core->objPalette_ : nullptr;
}

extern "C" size_t gb_port_audio_frame(gb_session_t *session, int16_t *out, size_t capacity) {
    if (!session || !session->core || !out || capacity < AUDIO_SAMPLES) return 0;
    minigb_apu_audio_callback(&session->apu, session->stereo);
    for (size_t i = 0; i < AUDIO_SAMPLES; i++) {
        int32_t left = session->stereo[i * 2];
        int32_t right = session->stereo[i * 2 + 1];
        out[i] = (int16_t)((left + right) / 2);
    }
    return AUDIO_SAMPLES;
}

extern "C" size_t gb_port_sram_size(const gb_session_t *session) {
    return session && session->core ? session->core->sramPersistSize_ : 0;
}

extern "C" bool gb_port_has_battery(const gb_session_t *session) {
    return session && session->battery;
}

extern "C" bool gb_port_needs_save(const gb_session_t *session) {
    return session && session->core && session->battery &&
           (session->rtc || session->core->sramDirty_);
}

extern "C" void gb_port_mark_saved(gb_session_t *session) {
    if (session && session->core) session->core->sramDirty_ = false;
}

extern "C" bool gb_port_export_sram(const gb_session_t *session,
                                    void *dst, size_t bytes) {
    if (!session || !session->core || !dst ||
        bytes != session->core->sramPersistSize_) return false;
    memcpy(dst, session->core->sramData, bytes);
    return true;
}

extern "C" bool gb_port_import_sram(gb_session_t *session,
                                    const void *src, size_t bytes) {
    if (!session || !session->core || !src ||
        bytes != session->core->sramPersistSize_) return false;
    memcpy(session->core->sramData, src, bytes);
    session->core->sramDirty_ = true;
    return true;
}

extern "C" bool gb_port_read_sram(const gb_session_t *session, size_t offset,
                                    void *dst, size_t bytes) {
    if (!session || !session->core || !dst ||
        offset > session->core->sramPersistSize_ ||
        bytes > session->core->sramPersistSize_ - offset) return false;
    memcpy(dst, session->core->sramData + offset, bytes);
    return true;
}

extern "C" bool gb_port_write_sram(gb_session_t *session, size_t offset,
                                     const void *src, size_t bytes) {
    if (!session || !session->core || !src ||
        offset > session->core->sramPersistSize_ ||
        bytes > session->core->sramPersistSize_ - offset) return false;
    if (memcmp(session->core->sramData + offset, src, bytes) != 0) {
        memcpy(session->core->sramData + offset, src, bytes);
        session->core->sramDirty_ = true;
    }
    return true;
}

extern "C" bool gb_port_export_rtc(gb_session_t *session, uint8_t dst[10]) {
    if (!session || !session->core || !dst) return false;
    GBEmulator *core = session->core;
    core->tickRTC();
    dst[0] = core->rtcSec_;
    dst[1] = core->rtcMin_;
    dst[2] = core->rtcHour_;
    dst[3] = core->rtcDay_;
    dst[4] = core->rtcDayHi_;
    memcpy(dst + 5, core->rtcLatched_, 5);
    return true;
}

extern "C" bool gb_port_import_rtc(gb_session_t *session, const uint8_t src[10]) {
    if (!session || !session->core || !src) return false;
    GBEmulator *core = session->core;
    core->rtcSec_ = src[0];
    core->rtcMin_ = src[1];
    core->rtcHour_ = src[2];
    core->rtcDay_ = src[3];
    core->rtcDayHi_ = src[4];
    memcpy(core->rtcLatched_, src + 5, 5);
    core->rtcLastTickMs_ = 0;
    return true;
}

extern "C" void gb_port_destroy(gb_session_t *session) {
    if (!session) return;
    delete session->core;
    free(session->bank0);
    free(session->cache);
    free(session->cgb_vram);
    free(session->cgb_wram);
    delete session;
}
