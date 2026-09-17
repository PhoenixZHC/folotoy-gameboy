#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "gb_emulator.h"

static uint32_t seed = 0x12345678;
static uint8_t next_byte() {
    seed = seed * 1664525u + 1013904223u;
    return (uint8_t)(seed >> 24);
}

static uint8_t reference_pixel(const GBEmulator &core, int x, int line, int window_line) {
    int source_x = (x + core.scx_) & 255;
    int source_y = (line + core.scy_) & 255;
    uint16_t map_base = (core.lcdc_ & 0x08) ? 0x1c00 : 0x1800;
    uint16_t data_base = (core.lcdc_ & 0x10) ? 0 : 0x0800;
    if ((core.lcdc_ & 0x20) && line >= core.wy_ && core.wx_ <= 166 && x >= (int)core.wx_ - 7) {
        source_x = x - ((int)core.wx_ - 7);
        source_y = window_line;
        map_base = (core.lcdc_ & 0x40) ? 0x1c00 : 0x1800;
    }
    uint8_t tile = core.vram[map_base + (source_y / 8) * 32 + source_x / 8];
    uint16_t tile_addr = data_base + ((core.lcdc_ & 0x10) ? tile : ((int8_t)tile + 128)) * 16;
    uint8_t lo = core.vram[tile_addr + (source_y % 8) * 2];
    uint8_t hi = core.vram[tile_addr + (source_y % 8) * 2 + 1];
    int bit = 7 - source_x % 8;
    uint8_t index = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
    return core.shadeRemap_[(core.bgp_ >> (index * 2)) & 3];
}

int main() {
    GBEmulator core;
    core.vram = (uint8_t *)malloc(GBEmulator::GB_VRAM_SIZE);
    core.framebuf = (uint8_t *)malloc(GBEmulator::GB_FB_SIZE);
    assert(core.vram && core.framebuf);
    for (int i = 0; i < (int)GBEmulator::GB_VRAM_SIZE; i++) core.vram[i] = next_byte();
    const int scrolls[] = {0, 1, 7, 8, 31, 127, 255};
    const int window_x[] = {0, 7, 8, 50, 166, 167};
    const int lines[] = {0, 1, 7, 8, 50, 143};
    for (int flags = 0; flags < 16; flags++) {
        core.lcdc_ = 0x81 | ((flags & 1) ? 0x08 : 0) |
                     ((flags & 2) ? 0x10 : 0) | ((flags & 4) ? 0x20 : 0) |
                     ((flags & 8) ? 0x40 : 0);
        for (int scroll : scrolls) {
            core.scx_ = (uint8_t)scroll;
            core.scy_ = (uint8_t)(scroll * 17);
            for (int wx : window_x) {
                core.wx_ = (uint8_t)wx;
                core.wy_ = 8;
                core.bgp_ = next_byte();
                core.shadeRemap_[0] = 3;
                core.shadeRemap_[1] = 1;
                core.shadeRemap_[2] = 0;
                core.shadeRemap_[3] = 2;
                for (int line : lines) {
                    core.windowLine_ = 13;
                    core.renderLine(line);
                    for (int x = 0; x < GB_W; x++) {
                        uint8_t actual = (core.framebuf[line * GBEmulator::GB_FB_STRIDE + x / 4] >>
                                          ((x & 3) * 2)) & 3;
                        assert(actual == reference_pixel(core, x, line, 13));
                    }
                    bool window_drawn = (core.lcdc_ & 0x20) && line >= core.wy_ && core.wx_ <= 166;
                    assert(core.windowLine_ == 13 + (window_drawn ? 1 : 0));
                }
            }
        }
    }
    return 0;
}
