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

int main() {
    test_hblank_poll();
    test_frame_cycles(false);
    test_frame_cycles(true);
    puts("GB timing tests: PASS");
}
