#include <assert.h>
#include <stdio.h>
#include <vector>
#include "gb_emulator.h"

// A game polling STAT must be able to observe HBlank while executing instructions.
static void test_hblank_poll() {
    std::vector<uint8_t> rom(32768, 0);
    const uint8_t code[] = {0xf0, 0x41, 0xe6, 0x03, 0x20, 0xfa,
                            0x3e, 0x5a, 0xea, 0x00, 0xc0, 0x18, 0xfe};
    memcpy(rom.data() + 0x150, code, sizeof(code));
    GBEmulator core;
    assert(core.loadRom("test", rom.data(), rom.size()) && core.init());
    core.pc_ = 0x150;
    core.runFrame();
    assert(core.wram[0] == 0x5a);
}

static void test_frame_cycles(bool halt) {
    std::vector<uint8_t> rom(32768, 0);
    GBEmulator core;
    assert(core.loadRom("test", rom.data(), rom.size()) && core.init());
    core.pc_ = 0x150;
    core.halted_ = halt;
    core.runFrame();
    assert(((unsigned)core.divReg_ * 256 + core.divCounter_) == (70224 % 65536));
}

static void test_lcd_off_and_restart() {
    std::vector<uint8_t> rom(32768, 0);
    const uint8_t loop[] = {0x04, 0x18, 0xfd}; // INC B; JR back: CPU must still run.
    memcpy(rom.data() + 0x150, loop, sizeof(loop));
    GBEmulator core;
    assert(core.loadRom("test", rom.data(), rom.size()) && core.init());
    core.pc_ = 0x150;
    memset(core.framebuf, 0xff, GBEmulator::GB_FB_SIZE);
    core.writeByte(0xff40, 0);
    core.iflag_ = 0;
    core.writeByte(0xff41, 0x78); // No STAT interrupt, including register writes.
    core.writeByte(0xff45, 0);
    assert((core.iflag_ & 3) == 0);
    core.writeByte(0xff07, 5); // Timer runs while the PPU is stopped.
    core.runFrame();
    assert(core.pc_ >= 0x150 && core.pc_ <= 0x152);
    assert(core.readByte(0xff44) == 0 && (core.readByte(0xff41) & 3) == 0);
    assert((core.iflag_ & 3) == 0 && (core.iflag_ & 4));
    for (unsigned i = 0; i < GBEmulator::GB_FB_SIZE; i++) assert(core.framebuf[i] == 0);
    core.writeByte(0xff41, 0);
    core.writeByte(0xff40, 0x91);
    core.iflag_ = 0;
    core.runFrame();
    assert(core.iflag_ & 1); // VBlank resumes only after LCD enable.
}

static void test_disable_during_frame() {
    std::vector<uint8_t> rom(32768, 0);
    const uint8_t code[] = {
        0xf0, 0x44, 0xfe, 0x05, 0x38, 0xfa, // wait for LY >= 5
        0xaf, 0xe0, 0x40,                   // disable LCD
        0x3e, 0x5a, 0xea, 0x00, 0xc0, 0x18, 0xfe,
    };
    memcpy(rom.data() + 0x150, code, sizeof(code));
    GBEmulator core;
    assert(core.loadRom("test", rom.data(), rom.size()) && core.init());
    core.pc_ = 0x150;
    core.runFrame();
    assert(core.wram[0] == 0x5a);
    assert(core.ly_ == 0 && core.ppuMode_ == 0 && !(core.iflag_ & 3));
    unsigned ticks = (unsigned)core.divReg_ * 256 + core.divCounter_;
    assert(ticks >= 70224 % 65536 && ticks <= 70224 % 65536 + 24);
}

static void test_enable_during_frame() {
    std::vector<uint8_t> rom(32768, 0);
    const uint8_t code[] = {
        0x3e, 0x91, 0xe0, 0x40,          // enable LCD
        0xf0, 0x44, 0xea, 0x00, 0xc0,    // first LY must be zero
        0xf0, 0x44, 0xfe, 0x01, 0x38, 0xfa,
        0x3e, 0x5a, 0xea, 0x01, 0xc0, 0x18, 0xfe,
    };
    memcpy(rom.data() + 0x150, code, sizeof(code));
    GBEmulator core;
    assert(core.loadRom("test", rom.data(), rom.size()) && core.init());
    memset(core.vram, 0xff, 16); // tile zero is dark when drawing resumes
    core.pc_ = 0x150;
    core.writeByte(0xff40, 0);
    core.runFrame();
    assert(core.wram[0] == 0 && core.wram[1] == 0x5a);
    for (unsigned i = 0; i < GBEmulator::GB_FB_SIZE; i++) assert(core.framebuf[i] == 0);
    core.runFrame();
    assert(core.framebuf[0] == 0xff);
}

static void test_repeated_lcd_toggle_budget() {
    std::vector<uint8_t> rom(32768, 0);
    const uint8_t code[] = {0x3e, 0, 0xe0, 0x40, 0x3e, 0x91, 0xe0, 0x40, 0x18, 0xf6};
    memcpy(rom.data() + 0x150, code, sizeof(code));
    GBEmulator core;
    assert(core.loadRom("test", rom.data(), rom.size()) && core.init());
    core.pc_ = 0x150;
    for (unsigned frame = 1; frame <= 3; frame++) {
        core.runFrame();
        unsigned ticks = (unsigned)core.divReg_ * 256 + core.divCounter_;
        unsigned expected = frame * 70224 % 65536;
        assert(ticks >= expected && ticks <= expected + 24);
        assert(!(core.iflag_ & 3));
    }
}

static void test_cgb_lcd_off() {
    std::vector<uint8_t> rom(32768, 0);
    uint8_t vram1[0x2000], wramExtra[0x6000];
    GBEmulator core;
    core.enableCgbMode(vram1, wramExtra);
    assert(core.loadRom("test", rom.data(), rom.size()) && core.init());
    core.halted_ = true;
    core.doubleSpeed_ = true;
    memset(core.bgPalette_, 0, sizeof(core.bgPalette_)); // palette zero is black
    memset(core.colorFramebuf_, 7, GB_W * GB_H);
    core.writeByte(0xff40, 0);
    core.runFrame();
    assert(core.ly_ == 0 && core.ppuMode_ == 0 && !(core.iflag_ & 3));
    assert(((unsigned)core.divReg_ * 256 + core.divCounter_) == 140448 % 65536);
    for (unsigned i = 0; i < GB_W * GB_H; i++) assert(core.colorFramebuf_[i] == GB_PORT_COLOR_BLANK);
}

static void test_window_clock_with_frame_skip() {
    std::vector<uint8_t> rom(32768, 0);
    const uint8_t code[] = {
        0x0e, 9, 0x06, 255, 0x05, 0x20, 0xfd, 0x0d, 0x20, 0xf8,
        0x3e, 0xb1, 0xe0, 0x40, 0x18, 0xfe,
    }; // Delay LCD enable by about 80 lines, so host frames end in visible scanout.
    memcpy(rom.data() + 0x150, code, sizeof(code));
    GBEmulator drawn, skipped;
    for (GBEmulator *core : {&drawn, &skipped}) {
        assert(core->loadRom("test", rom.data(), rom.size()) && core->init());
        core->pc_ = 0x150;
        core->wx_ = 7;
        core->wy_ = 0;
        core->bgp_ = 0xe4;
        for (int row = 0; row < 8; row++) {
            core->vram[2 * row] = row & 1 ? 0xff : 0;
            core->vram[2 * row + 1] = row & 2 ? 0xff : 0;
        }
        core->writeByte(0xff40, 0); // ROM re-enables partway through the host frame.
    }
    for (unsigned frame = 0; frame < 6; frame++) {
        skipped.renderThisFrame_ = frame % 2 == 0;
        drawn.runFrame();
        skipped.runFrame();
        assert(drawn.ly_ > 20 && drawn.ly_ < 120);
        assert(drawn.ly_ == skipped.ly_);
        assert(drawn.windowLine_ == skipped.windowLine_);
        if (skipped.renderThisFrame_)
            assert(!memcmp(drawn.framebuf, skipped.framebuf, GBEmulator::GB_FB_SIZE));
    }
}

int main() {
    test_hblank_poll();
    test_frame_cycles(false);
    test_frame_cycles(true);
    test_lcd_off_and_restart();
    test_disable_during_frame();
    test_enable_during_frame();
    test_repeated_lcd_toggle_budget();
    test_cgb_lcd_off();
    test_window_clock_with_frame_skip();
    puts("GB timing tests: PASS");
}
