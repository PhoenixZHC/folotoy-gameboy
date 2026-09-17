#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "gb_emulator.h"

int main() {
    GBEmulator core;
    core.vram = (uint8_t *)calloc(1, 0x2000);
    core.vram1_ = (uint8_t *)calloc(1, 0x2000);
    core.colorFramebuf_ = (uint8_t *)calloc(1, GB_W * GB_H);
    assert(core.vram && core.vram1_ && core.colorFramebuf_);
    core.isCgb_ = true;
    core.lcdc_ = 0x91;
    core.scx_ = 0;
    core.scy_ = 0;
    core.vram[0x1800] = 1;
    core.vram1_[0x1800] = 0x0a; // tile bank 1, palette 2
    core.vram1_[16] = 0x80; // first pixel has color index 1
    core.renderLine(0);
    assert(core.colorFramebuf_[0] == 9);
    assert(core.colorFramebuf_[1] == 8);
    core.vram1_[0x1800] |= 0x20; // horizontal flip
    core.renderLine(0);
    assert(core.colorFramebuf_[0] == 8);
    assert(core.colorFramebuf_[7] == 9);

    core.vram1_[0x1800] = 0x0a;
    core.lcdc_ = 0x93; // enable sprites
    core.oam[0] = 16; // y=0
    core.oam[1] = 8;  // x=0
    core.oam[2] = 2;
    core.oam[3] = 0x0b; // tile bank 1, OBJ palette 3
    core.vram1_[32] = 0x80;
    core.renderLine(0);
    assert(core.colorFramebuf_[0] == 32 + 3 * 4 + 1);
    core.vram1_[0x1800] |= 0x80; // BG takes priority over OBJ on nonzero pixels
    core.renderLine(0);
    assert(core.colorFramebuf_[0] == 9);
    free(core.vram1_);
    core.vram1_ = nullptr;
    return 0;
}
