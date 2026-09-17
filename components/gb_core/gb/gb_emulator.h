#pragma once
/**
 * Game Boy / Game Boy Color Emulator
 *
 * Full SM83 CPU, MBC1/MBC2/MBC3/MBC5 + ROM-only, PPU with BG/Window/Sprites,
 * CGB mode (banked VRAM/WRAM, 8-palette BG+OBJ, KEY1 double-speed, HDMA),
 * MBC3 RTC with proper halt bit / 9-bit day counter / carry flag /
 * register writes, serial port stub, cheat lookup hook, bank streaming
 * cache for 1 MB+ ROMs, per-game e-ink patch framework.
 *
 * Save state format:
 *   SUM4  — includes SRAM dirty tracking, all CGB state (VRAM bank,
 *           WRAM bank, KEY1, BG/OBJ palette arrays, HDMA1-5, serial
 *           registers) plus the main CPU/PPU/timer state.
 * SRAM format:
 *   Raw SRAM (32 KB max) followed by an optional 10-byte MBC3 RTC
 *   trailer (5 live + 5 latched values). Older saves without the trailer
 *   load fine — the trailer bytes default to 0.
 *
 * Bank cache:
 *   The main.cpp load path decides whether the ROM fits entirely after
 *   the struct in emuBlock, or whether to use streaming mode with N×16KB
 *   slots (typically 4-8 slots for 1-2 MB ROMs).
 */

// Ported from the X4 SumiBoy standalone emulator into SUMI.
// The only platform adaptation is the ROM bank-cache file handle:
// SumiBoy used LittleFS (fs::File), SUMI reads ROMs from the SD card
// via SdFat (FsFile). The two share seek/read API shapes so the body
// of readCachedBank() is byte-identical — only the type differs.
#include "gb_emulator_base.h"
#include "pokemon_red_patch.h"
#include "gb_eink_patches.h"
#include "cheats.h"
#include <SdFat.h>

// GB constants
#define GB_W 160
#define GB_H 144
#define ROM_BANK_SIZE 0x4000   // 16KB
#define RAM_BANK_SIZE 0x2000   // 8KB

// MBC types
enum MBCType {
    MBC_NONE = 0,   // ROM only (no banking)
    MBC_MBC1,       // MBC1 (Pokemon R/B, Zelda LA)
    MBC_MBC2,       // MBC2 (Final Fantasy Adventure, Mega Man)
    MBC_MBC3,       // MBC3 (Pokemon G/S/C)
    MBC_MBC5        // MBC5 (most GBC games)
};

// Buffer sizes. Kept as constants so the heap-pointer allocation in init()
// can reference them and so save-state I/O knows how much to read/write.
// AI Passport 移植按卡带头实际容量分配 SRAM。超过 32 KB 的卡带在
// loadRom() 中明确拒绝，不能把较大的存档静默截断到 8 KB。

class GBEmulator : public Emulator {
public:
    // Buffer size constants (must be constexpr — used in member init below
    // and in allocation/save paths).
    // FB is packed 2 bits per pixel (4 shades = 2 bits, 4 pixels per byte).
    // 160×144 / 4 = 5760 bytes, down from 23040 — saves 17 KB of heap, which
    // is what lets emu_->init() + loadROM() coexist on the ESP32-C3 heap
    // post-arena-release. Pixel reads go through framebufShade(x, y); writes
    // are batched per-scanline inside renderLine so the PPU inner loop still
    // writes to a dense 160-byte stack buffer.
    static constexpr uint32_t GB_FB_SIZE   = (GB_W * GB_H) / 4;   // 5760 bytes (2bpp packed)
    static constexpr uint32_t GB_FB_STRIDE = GB_W / 4;            // 40 bytes/row
    static constexpr uint32_t GB_VRAM_SIZE = 0x2000;              //  8 KB
    static constexpr uint32_t GB_WRAM_SIZE = 0x2000;              //  8 KB
    uint32_t sramSize_ = 1;  // 无 SRAM 卡带保留 1 字节，避免 malloc(0)
    uint32_t sramPersistSize_ = 0;
    bool sramDirty_ = false;
    bool romIoFailed_ = false;

    // ====== MEMORY ======
    // The big buffers are heap-allocated in init() rather than stored inline
    // in the struct. On ESP32-C3, sizeof(GBEmulator) with all buffers inline
    // was ~72 KB, and allocating that single contiguous chunk fragmented the
    // heap so badly that subsequent ROM/bank-cache mallocs failed. By
    // splitting each buffer into its own malloc we let the allocator place
    // them in whatever free regions fit, and we keep the 77 KB "largest
    // contiguous" block available for the ROM/bank-cache allocations the
    // wrapper does later.
    uint8_t* framebuf = nullptr;       // GB_FB_SIZE bytes, 2bpp packed (4 pixels/byte)
    uint8_t* colorFramebuf_ = nullptr; // CGB only: palette indices, one byte/pixel
    uint8_t* vram     = nullptr;       // GB_VRAM_SIZE bytes
    uint8_t* wram     = nullptr;       // GB_WRAM_SIZE bytes
    uint8_t  oam[0xA0] = {};           // 160 bytes OAM  (small — stays inline)
    uint8_t  hram[0x80] = {};          // 128 bytes high RAM (small — inline)
    uint8_t* sramData = nullptr;       // GB_SRAM_SIZE bytes

    // ====== ROM ACCESS ======
    const uint8_t* romData_ = nullptr;  // Directly addressable ROM, possibly mapped Flash
    uint32_t romSize_ = 0;
    uint32_t romBankCount_ = 0;
    bool romInRam_ = false;
    const uint8_t* mappedRomBanks_[2] = {nullptr, nullptr};
    FsFile romFile_;                     // Open file for bank cache reads (SdFat)

    // Bank cache for large ROMs
    uint8_t* romBank0_ = nullptr;       // Bank 0 always in RAM
    uint8_t* bankCache_ = nullptr;
    int cacheSlots_ = 0;
    int* cacheMap_ = nullptr;
    int cacheNextSlot_ = 0;

    // ====== MBC STATE ======
    MBCType mbcType_ = MBC_MBC3;
    uint16_t romBank_ = 1;              // Current ROM bank (16-bit for MBC5)
    uint8_t ramBank_ = 0;
    bool ramEnabled_ = false;
    uint8_t mbc1Mode_ = 0;              // MBC1: 0=ROM banking, 1=RAM banking
    uint8_t mbc1Bank2_ = 0;             // MBC1: upper 2-bit register

    // ====== CPU REGISTERS ======
    uint16_t pc_, sp_;
    uint8_t a_, f_, b_, c_, d_, e_, h_, l_;
    bool ime_ = false;
    bool halted_ = false;
    bool haltBug_ = false;
    bool eiPending_ = false;
    uint8_t ie_ = 0, iflag_ = 0;

    // ====== LCD REGISTERS ======
    uint8_t lcdc_, lcdstat_, scy_, scx_, ly_, lyc_;
    uint8_t bgp_, obp0_, obp1_, wy_, wx_;
    uint8_t ppuMode_ = 0;  // 0=HBlank, 1=VBlank, 2=OAM, 3=Transfer

    // ====== TIMER ======
    uint8_t tima_ = 0, tma_ = 0, tac_ = 0;
    uint16_t timerCounter_ = 0;
    uint16_t divCounter_ = 0;
    uint8_t divReg_ = 0;

    // ====== JOYPAD ======
    uint8_t joyDpad_ = 0x0F;
    uint8_t joyButtons_ = 0x0F;
    uint8_t joypadSelect_ = 0;
    uint8_t prevInputButtons_ = 0;  // For edge-detect joypad interrupt

    // Serial port (link cable) stub. We don't actually communicate, but
    // a few games (Tetris end screen, Pokemon after Oak's intro if serial
    // ever gets touched, Mario's Picross printer mode) busy-wait for the
    // serial interrupt after writing to $FF02. Completing writes instantly
    // and firing the IRQ keeps them from deadlocking.
    uint8_t sbReg_ = 0xFF;
    uint8_t scReg_ = 0x7E;

    // ====== FRAME SKIP (PPU-Lite) ======
    bool renderThisFrame_ = true;   // Only render pixels on the last frame before display

    // ====== WINDOW LINE COUNTER ======
    int windowLine_ = 0;  // Internal window scanline counter

    // ====== STAT edge detection ======
    bool prevStatLine_ = false;

    // ====== SOUND (stub) ======
    uint8_t soundRegs_[0x30];
    void *soundContext_ = nullptr;
    void (*soundWrite_)(void *, uint16_t, uint8_t) = nullptr;
    uint8_t (*soundRead_)(void *, uint16_t) = nullptr;

    // Per-game patch flags. Set externally before bank cache use begins.
    // When true, every freshly-loaded bank gets run through pokered::patchBank
    // to apply e-ink optimization patches in-flight. The original ROM file
    // on disk is never touched.
    bool pokemonRedPatch_ = false;

    // Per-bank generalized e-ink patch framework flag. Set by main.cpp's
    // bank-cached load path when gbpatches::isPatchedGame(romBank0_) returns
    // true, so the freshly-loaded bank gets its scheduled patches applied
    // every time it's brought into the cache. Orthogonal to the Pokemon Red
    // flag — a single ROM may trigger one or the other but not both.
    bool gbPatchEnabled_ = false;

    // E-ink palette remap. Game Boy outputs 4 shades (0-3); this LUT remaps
    // each shade to a different value before dithering. The default
    // identity preserves the existing behavior; user-selected presets can
    // change shade balance for high contrast, night reading, etc.
    uint8_t shadeRemap_[4] = { 0, 1, 2, 3 };

    // ====== MBC3 RTC stub ======
    // Pokemon Gold/Silver/Crystal use the MBC3 RTC for day/night cycles
    // and time-of-day events. We don't have an actual RTC, but tracking
    // a virtual clock that ticks based on millis() lets these features
    // work approximately. Reads to the RTC registers ($08-$0C as ramBank_)
    // return the current virtual time.
    uint8_t rtcSec_ = 0;
    uint8_t rtcMin_ = 0;
    uint8_t rtcHour_ = 0;
    uint8_t rtcDay_ = 0;
    uint8_t rtcDayHi_ = 0;
    uint8_t rtcLatched_[5] = {};   // Latched values when game writes 0→1 to $6000
    bool rtcLatchPrev_ = false;
    unsigned long rtcLastTickMs_ = 0;

    // Tick the virtual RTC based on real elapsed time. Called from runFrames.
    //
    // MBC3 RTC day counter is a 9-bit value:
    //   rtcDayHi_ layout: [carry:7][halt:6][reserved:5..1][day8:0]
    //   total days = (rtcDayHi_ & 1) * 256 + rtcDay_, range 0..511.
    // On overflow from 511→0 we set the carry bit (bit 7); games clear
    // it manually. The halt bit (bit 6), when set, freezes the RTC —
    // we still advance rtcLastTickMs_ so unhalting doesn't "catch up"
    // the time spent halted.
    inline void tickRTC() {
        unsigned long now = millis();
        if (rtcLastTickMs_ == 0) { rtcLastTickMs_ = now; return; }

        // Halt bit set: freeze RTC but keep the anchor current so the
        // clock doesn't jump forward when the game un-halts it.
        if (rtcDayHi_ & 0x40) { rtcLastTickMs_ = now; return; }

        uint32_t elapsed = (uint32_t)(now - rtcLastTickMs_) / 1000;
        if (elapsed == 0) return;
        rtcLastTickMs_ += elapsed * 1000u;
        uint64_t day = (uint16_t)rtcDay_ | ((uint16_t)(rtcDayHi_ & 1u) << 8);
        uint64_t total = day * 86400u + (uint32_t)rtcHour_ * 3600u +
                         (uint32_t)rtcMin_ * 60u + rtcSec_ + elapsed;
        uint64_t days = total / 86400u;
        if (days >= 512) rtcDayHi_ |= 0x80;
        day = days % 512;
        total %= 86400u;
        rtcHour_ = (uint8_t)(total / 3600u);
        rtcMin_ = (uint8_t)((total % 3600u) / 60u);
        rtcSec_ = (uint8_t)(total % 60u);
        rtcDay_ = (uint8_t)day;
        rtcDayHi_ = (rtcDayHi_ & 0xc0u) | (uint8_t)(day >> 8);
    }

    // ====== GAME BOY COLOR (CGB) MODE ======
    // Detected from ROM header byte 0x143:
    //   0x80 = CGB-compatible (also runs on DMG)
    //   0xC0 = CGB-only
    // When isCgb_ is true, vram1_ + wramExtra_ point at heap-allocated buffers
    // (32 KB total, allocated by main.cpp before calling loadRom).
    // The AI Passport renderer stores palette indices in colorFramebuf_.
    bool isCgb_ = false;
    uint8_t* vram1_ = nullptr;        // VRAM bank 1 (8 KB) — heap, only if CGB
    uint8_t* wramExtra_ = nullptr;    // WRAM banks 2-7 (24 KB) — heap, only if CGB
    uint8_t vramBank_ = 0;            // VBK $FF4F (0 or 1)
    uint8_t wramBank_ = 1;            // SVBK $FF70 (1-7; 0 maps to 1)
    uint8_t key1_ = 0;                // KEY1 $FF4D — speed switch register
    bool doubleSpeed_ = false;        // True after STOP triggered speed switch
    uint8_t bcpsReg_ = 0;             // BCPS $FF68 — BG palette index/auto-increment
    uint8_t ocpsReg_ = 0;             // OCPS $FF6A — OBJ palette index/auto-increment
    uint8_t bgPalette_[64] = {};      // 8 palettes × 4 colors × 2 bytes
    uint8_t objPalette_[64] = {};     // ditto for sprites
    // HDMA registers (CGB only). We implement these as immediate general DMA
    // rather than HBlank-synced — most games work fine, some have visual seams.
    uint8_t hdma1_ = 0, hdma2_ = 0, hdma3_ = 0, hdma4_ = 0, hdma5_ = 0xFF;

    // Decode a GBC 15-bit RGB color (5R + 5G + 5B little-endian word
    // at the given index in the BG or OBJ palette) to a 4-shade
    // greyscale value. Uses the standard luminance formula
    //   Y = 0.299*R + 0.587*G + 0.114*B
    // scaled to 0..3 for our 4-shade output.
    //
    // Currently unused by the renderer (which still routes everything
    // through the DMG monochrome BGP register), but available for
    // a future per-tile-attribute renderer rewrite.
    uint8_t cgbPaletteToShade(const uint8_t* pal, int colorIdx) {
        uint16_t rgb = pal[colorIdx * 2] | (pal[colorIdx * 2 + 1] << 8);
        int r = (rgb >> 0)  & 0x1F;
        int g = (rgb >> 5)  & 0x1F;
        int b = (rgb >> 10) & 0x1F;
        // Luminance ≈ 0.299*R + 0.587*G + 0.114*B (×1024 to keep ints)
        int lum = (306 * r + 601 * g + 117 * b) >> 10;  // 0..30 ish
        if (lum < 8)  return 0;
        if (lum < 16) return 1;
        if (lum < 24) return 2;
        return 3;
    }

    // E-ink palette themes — pre-baked shade remap tables. Selected via
    // setShadeTheme(). All themes preserve the binary contract that 0 is
    // "background" (lightest) and 3 is "ink" (darkest); only the mid-tone
    // mapping changes.
    enum ShadeTheme {
        SHADE_DEFAULT = 0,    // 0,1,2,3 (linear, all four shades)
        SHADE_HIGH_CONTRAST,  // 0,0,3,3 (collapse to pure B&W)
        SHADE_INVERTED,       // 3,2,1,0 (white text on black background)
        SHADE_LIGHT,          // 0,0,1,3 (push everything brighter)
        SHADE_THEME_COUNT
    };

    void setShadeTheme(ShadeTheme t) {
        static const uint8_t themes[SHADE_THEME_COUNT][4] = {
            { 0, 1, 2, 3 },  // DEFAULT
            { 0, 0, 3, 3 },  // HIGH_CONTRAST
            { 3, 2, 1, 0 },  // INVERTED
            { 0, 0, 1, 3 },  // LIGHT
        };
        if (t >= 0 && t < SHADE_THEME_COUNT) {
            for (int i = 0; i < 4; i++) shadeRemap_[i] = themes[t][i];
        }
    }

    // Enable Game Boy Color mode. Pass pre-allocated heap buffers for the
    // extra VRAM bank 1 (8 KB) and the extra WRAM banks 2-7 (24 KB). Once
    // called, the emulator's memory bus consults vramBank_/wramBank_ to
    // route reads/writes to the right buffer.
    //
    // Caller (main.cpp) is responsible for the buffer lifetime — these are
    // typically heap allocations that get freed in destroyEmulator().
    void enableCgbMode(uint8_t* vram1Buf, uint8_t* wramExtraBuf) {
        isCgb_ = true;
        vram1_ = vram1Buf;
        wramExtra_ = wramExtraBuf;
        if (vram1_) memset(vram1_, 0, 0x2000);
        if (wramExtra_) memset(wramExtra_, 0, 0x6000);
        Serial.println("[GB] CGB mode enabled (banked WRAM/VRAM available)");
    }

    // ====== Emulator interface ======
    // Allocate the big heap buffers (framebuf/vram/wram/sramData). Kept
    // out of the struct because a single 72 KB allocation fragments the
    // ESP32-C3 heap so badly that subsequent 32 KB+ ROM mallocs fail.
    // The wrapper reset-path calls this once per emulator instance; the
    // buffers persist until the destructor runs.
    //
    // With the 2bpp-packed framebuf the four buffers total ~30 KB (6 + 8 +
    // 8 + 8), down from 47 KB before. They're allocated 8 KB first so they
    // slot into fragments where possible, then the 6 KB packed framebuf
    // last — that preserves the 45 KB+ contiguous block the wrapper's
    // earlier ROM / bank-cache mallocs needed.
    bool init() override {
        if (!vram)     vram     = (uint8_t*)malloc(GB_VRAM_SIZE);
        if (!wram)     wram     = (uint8_t*)malloc(GB_WRAM_SIZE);
        if (!sramData) sramData = (uint8_t*)malloc(sramSize_);
        if (!isCgb_ && !framebuf) framebuf = (uint8_t*)malloc(GB_FB_SIZE);
        if (isCgb_ && !colorFramebuf_)
            colorFramebuf_ = (uint8_t*)malloc(GB_W * GB_H);
        if ((!isCgb_ && !framebuf) || !vram || !wram || !sramData ||
            (isCgb_ && !colorFramebuf_)) {
            // Partial allocation on failure — the destructor cleans up
            // whichever succeeded. Caller gets false and aborts.
            return false;
        }
        reset();
        return true;
    }

    ~GBEmulator() {
        if (framebuf) { free(framebuf); framebuf = nullptr; }
        if (colorFramebuf_) { free(colorFramebuf_); colorFramebuf_ = nullptr; }
        if (vram)     { free(vram);     vram = nullptr; }
        if (wram)     { free(wram);     wram = nullptr; }
        if (sramData) { free(sramData); sramData = nullptr; }
    }

    bool loadRom(const char* path, const uint8_t* data, uint32_t size) override {
        (void)path;
        romData_ = data;
        romSize_ = size;
        romBankCount_ = size / ROM_BANK_SIZE;
        if (romBankCount_ == 0) romBankCount_ = 1;
        romInRam_ = (data != nullptr);

        // Detect MBC type from ROM header byte 0x0147
        uint8_t cartType = 0;
        if (romInRam_ && romSize_ > 0x0148) {
            cartType = romData_[0x0147];
        } else if (!romInRam_ && romBank0_ && romSize_ > 0x0148) {
            cartType = romBank0_[0x0147];
        }
        detectMBC(cartType);
        const uint8_t *header = romInRam_ ? romData_ : romBank0_;
        if (!header || size < 0x150) return false;
        static constexpr uint32_t kRamSizes[] = {0, 2048, 8192, 32768, 131072, 65536};
        uint8_t ramCode = header[0x149];
        if (ramCode >= sizeof(kRamSizes) / sizeof(kRamSizes[0])) return false;
        sramPersistSize_ = mbcType_ == MBC_MBC2 ? 512 : kRamSizes[ramCode];
        if (sramPersistSize_ > 32768) return false;
        sramSize_ = sramPersistSize_ ? sramPersistSize_ : 1;

        Serial.printf("[GB] MBC type: %d, ROM banks: %u, cart type: 0x%02X\n",
                      mbcType_, romBankCount_, cartType);
        refreshMappedRomBanks();
        return true;
    }

    void detectMBC(uint8_t cartType) {
        switch (cartType) {
            case 0x00:                             // ROM only
                mbcType_ = MBC_NONE;
                break;
            case 0x01: case 0x02: case 0x03:       // MBC1
                mbcType_ = MBC_MBC1;
                break;
            case 0x05: case 0x06:                   // MBC2 + battery
                mbcType_ = MBC_MBC2;
                break;
            case 0x0F: case 0x10: case 0x11:       // MBC3 + Timer
            case 0x12: case 0x13:                   // MBC3
                mbcType_ = MBC_MBC3;
                break;
            case 0x19: case 0x1A: case 0x1B:       // MBC5
            case 0x1C: case 0x1D: case 0x1E:       // MBC5 + Rumble
                mbcType_ = MBC_MBC5;
                break;
            default:
                mbcType_ = MBC_NONE;
                break;
        }
    }

    void setInput(uint8_t buttons) override {
        joyDpad_ = 0x0F;
        joyButtons_ = 0x0F;

        if (buttons & INPUT_RIGHT)  joyDpad_ &= ~0x01;
        if (buttons & INPUT_LEFT)   joyDpad_ &= ~0x02;
        if (buttons & INPUT_UP)     joyDpad_ &= ~0x04;
        if (buttons & INPUT_DOWN)   joyDpad_ &= ~0x08;
        if (buttons & INPUT_A)      joyButtons_ &= ~0x01;  // A
        if (buttons & INPUT_B)      joyButtons_ &= ~0x02;  // B
        if (buttons & INPUT_SELECT) joyButtons_ &= ~0x04;  // Select
        if (buttons & INPUT_START)  joyButtons_ &= ~0x08;  // Start

        // Joypad interrupt on high-to-low transition (new button press edge)
        uint8_t newPresses = buttons & ~prevInputButtons_;
        if (newPresses) iflag_ |= 0x10;
        prevInputButtons_ = buttons;
    }

    uint8_t* getFramebuffer() override { return framebuf; }
    int getWidth() override { return GB_W; }
    int getHeight() override { return GB_H; }
    int getWarmupFrames() override { return 600; }

    void runFrames(int count) override {
        for (int i = 0; i < count; i++) {
            // PPU-Lite: only render pixels on the last frame (displayed one)
            // Earlier frames still run CPU, timers, interrupts — just skip tile fetches
            renderThisFrame_ = (i == count - 1);
            runFrame();
            frameCount++;
        }
    }

    void reset() override {
        // DMG post-boot register state
        a_ = 0x01; f_ = 0xB0;
        b_ = 0x00; c_ = 0x13;
        d_ = 0x00; e_ = 0xD8;
        h_ = 0x01; l_ = 0x4D;
        pc_ = 0x100; sp_ = 0xFFFE;

        lcdc_ = 0x91; lcdstat_ = 0;
        scy_ = 0; scx_ = 0; ly_ = 0; lyc_ = 0;
        bgp_ = 0xFC; obp0_ = 0xFF; obp1_ = 0xFF;
        wy_ = 0; wx_ = 0;

        ime_ = false; halted_ = false; haltBug_ = false; eiPending_ = false;
        ie_ = 0; iflag_ = 0; ppuMode_ = 0; prevStatLine_ = false;

        tima_ = 0; tma_ = 0; tac_ = 0;
        timerCounter_ = 0; divCounter_ = 0; divReg_ = 0;

        romBank_ = 1; ramBank_ = 0; ramEnabled_ = false;
        mbc1Mode_ = 0; mbc1Bank2_ = 0;
        refreshMappedRomBanks();
        joyDpad_ = 0x0F; joyButtons_ = 0x0F; joypadSelect_ = 0;
        prevInputButtons_ = 0;
        windowLine_ = 0;

        // CGB register reset — without this, pause-menu Reset would leave
        // the bank registers in whatever state the last play session
        // ended in. Reset reproduces post-boot state: WRAM bank 1, VRAM
        // bank 0, single speed, palettes cleared.
        vramBank_ = 0;
        wramBank_ = 1;
        key1_ = 0;
        doubleSpeed_ = false;
        bcpsReg_ = 0;
        ocpsReg_ = 0;
        memset(bgPalette_, 0, sizeof(bgPalette_));
        memset(objPalette_, 0, sizeof(objPalette_));
        hdma1_ = hdma2_ = hdma3_ = hdma4_ = 0;
        hdma5_ = 0xFF;

        // Serial port stub registers reset to idle state. sbReg_ is the
        // transfer buffer (no peer = 0xFF), scReg_ is the transfer control
        // (0x7E means "external clock, no transfer in progress", matching
        // the unused-bit mask we return on reads).
        sbReg_ = 0xFF;
        scReg_ = 0x7E;

        // framebuf/vram/wram/sramData are pointers to heap buffers now —
        // sizeof() would return pointer size, not buffer size. Use the
        // explicit byte counts from the class constants. null checks guard
        // the case where init() hasn't completed allocation yet.
        if (framebuf) memset(framebuf, 0, GB_FB_SIZE);
        if (colorFramebuf_) memset(colorFramebuf_, 0, GB_W * GB_H);
        if (vram)     memset(vram,     0, GB_VRAM_SIZE);
        if (wram)     memset(wram,     0, GB_WRAM_SIZE);
        memset(oam, 0, sizeof(oam));
        memset(hram, 0, sizeof(hram));
        if (sramData) memset(sramData, 0, sramSize_);
        memset(soundRegs_, 0, sizeof(soundRegs_));

        // Wipe the extra GBC heap buffers too (only present in CGB mode).
        // Without this, Reset Game would leave banked WRAM/VRAM populated
        // with stale data from the previous session.
        if (vram1_) memset(vram1_, 0, 0x2000);
        if (wramExtra_) memset(wramExtra_, 0, 0x6000);

        soundRegs_[0x16] = 0xF1;  // NR52 at FF26
        frameCount = 0;
    }

    // ====== Save/Load ======
    // GB_SAVE_MAGIC bumped from SUM3 → SUM4 when CGB-specific fields were
    // added to the save state. Old SUM3 files are deleted on load (the
    // loader's magic check removes mismatched files automatically), so
    // users lose one generation of save states but gain working CGB save
    // states afterward. This is a one-time migration cost.
    #define GB_SAVE_MAGIC 0x53554D34  // "SUM4" - v4: + CGB state + serial

    struct SaveHeader {
        uint32_t magic;
        uint16_t pc, sp;
        uint8_t a, f, b, c, d, e, h, l;
        uint8_t ime, halted, haltBug, eiPending;
        uint8_t ie, iflag;
        uint8_t lcdc, lcdstat, scy, scx, ly, lyc;
        uint8_t bgp, obp0, obp1, wy, wx;
        uint8_t tima, tma, tac, divReg;
        uint16_t timerCounter, divCounter;
        uint16_t romBank;
        uint8_t ramBank, ramEnabled;
        uint8_t joypadSelect;
        uint8_t mbcType;
        uint8_t mbc1Mode, mbc1Bank2;
        uint8_t ppuMode;
        uint8_t prevStatLine;
        uint8_t soundRegs[0x30];
        uint32_t frameCount;

        // ── v4: CGB extensions + serial stub ──
        // isCgbSave is set when this save came from a game running in
        // CGB mode. Loading a SUM4 save with isCgbSave=0 on a DMG game
        // skips the CGB fields entirely (palette/wram arrays untouched).
        uint8_t isCgbSave;
        uint8_t vramBank;
        uint8_t wramBank;
        uint8_t key1;
        uint8_t doubleSpeed;
        uint8_t bcpsReg, ocpsReg;
        uint8_t bgPalette[64];
        uint8_t objPalette[64];
        uint8_t hdma1, hdma2, hdma3, hdma4, hdma5;
        uint8_t sbReg, scReg;
        uint8_t reserved[8];
    };

    bool saveState(SdFat& sd, const char* path) override {
        char tmpPath[84];
        buildTmpPath(path, tmpPath, sizeof(tmpPath));
        FsFile f = sd.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC);
        if (!f) return false;

        SaveHeader hdr = {};
        hdr.magic = GB_SAVE_MAGIC;
        hdr.pc = pc_; hdr.sp = sp_;
        hdr.a = a_; hdr.f = f_; hdr.b = b_; hdr.c = c_;
        hdr.d = d_; hdr.e = e_; hdr.h = h_; hdr.l = l_;
        hdr.ime = ime_; hdr.halted = halted_; hdr.haltBug = haltBug_;
        hdr.eiPending = eiPending_;
        hdr.ie = ie_; hdr.iflag = iflag_;
        hdr.lcdc = lcdc_; hdr.lcdstat = lcdstat_; hdr.scy = scy_; hdr.scx = scx_;
        hdr.ly = ly_; hdr.lyc = lyc_; hdr.bgp = bgp_; hdr.obp0 = obp0_; hdr.obp1 = obp1_;
        hdr.wy = wy_; hdr.wx = wx_;
        hdr.tima = tima_; hdr.tma = tma_; hdr.tac = tac_; hdr.divReg = divReg_;
        hdr.timerCounter = timerCounter_; hdr.divCounter = divCounter_;
        hdr.romBank = romBank_; hdr.ramBank = ramBank_; hdr.ramEnabled = ramEnabled_;
        hdr.joypadSelect = joypadSelect_;
        hdr.mbcType = (uint8_t)mbcType_;
        hdr.mbc1Mode = mbc1Mode_; hdr.mbc1Bank2 = mbc1Bank2_;
        hdr.ppuMode = ppuMode_;
        hdr.prevStatLine = prevStatLine_ ? 1 : 0;
        memcpy(hdr.soundRegs, soundRegs_, sizeof(soundRegs_));
        hdr.frameCount = frameCount;

        // v4 additions: CGB state + serial port stub. When isCgbSave is 0
        // the CGB fields are still written (wasted bytes) but loaders
        // ignore them, so DMG-only saves stay valid on a CGB-capable
        // build and vice-versa.
        hdr.isCgbSave = isCgb_ ? 1 : 0;
        hdr.vramBank = vramBank_;
        hdr.wramBank = wramBank_;
        hdr.key1 = key1_;
        hdr.doubleSpeed = doubleSpeed_ ? 1 : 0;
        hdr.bcpsReg = bcpsReg_;
        hdr.ocpsReg = ocpsReg_;
        memcpy(hdr.bgPalette, bgPalette_, sizeof(bgPalette_));
        memcpy(hdr.objPalette, objPalette_, sizeof(objPalette_));
        hdr.hdma1 = hdma1_; hdr.hdma2 = hdma2_; hdr.hdma3 = hdma3_;
        hdr.hdma4 = hdma4_; hdr.hdma5 = hdma5_;
        hdr.sbReg = sbReg_; hdr.scReg = scReg_;

        f.write((uint8_t*)&hdr, sizeof(hdr));
        f.write(vram, GB_VRAM_SIZE);
        f.write(wram, GB_WRAM_SIZE);
        f.write(oam, sizeof(oam));
        f.write(hram, sizeof(hram));
        f.write(sramData, sramSize_);
        // v4 trailer: CGB VRAM bank 1 + extra WRAM banks. Only written
        // (and read) when the emulator is actually in CGB mode.
        if (isCgb_ && vram1_) f.write(vram1_, 0x2000);
        if (isCgb_ && wramExtra_) f.write(wramExtra_, 0x6000);
        f.sync();
        f.close();
        return safeRename(sd, tmpPath, path);
    }

    bool loadState(SdFat& sd, const char* path) override {
        if (!sd.exists(path)) return false;
        FsFile f = sd.open(path, O_RDONLY);
        if (!f) return false;

        SaveHeader hdr;
        if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
            f.close();
            sd.remove(path);
            Serial.println("[GB] Save state truncated, removed");
            return false;
        }
        if (hdr.magic != GB_SAVE_MAGIC) {
            f.close();
            sd.remove(path);
            Serial.printf("[GB] Save magic mismatch (got 0x%08X, want 0x%08X), removed\n",
                          hdr.magic, (uint32_t)GB_SAVE_MAGIC);
            return false;
        }

        // Reject save states from mismatched MBC type (e.g. corrupted saves)
        if (hdr.mbcType != (uint8_t)mbcType_) {
            Serial.printf("[GB] Save MBC mismatch: saved=%d current=%d, skipping\n",
                          hdr.mbcType, mbcType_);
            f.close();
            sd.remove(path);  // Delete the stale save
            return false;
        }

        pc_ = hdr.pc; sp_ = hdr.sp;
        a_ = hdr.a; f_ = hdr.f; b_ = hdr.b; c_ = hdr.c;
        d_ = hdr.d; e_ = hdr.e; h_ = hdr.h; l_ = hdr.l;
        ime_ = hdr.ime; halted_ = hdr.halted; haltBug_ = hdr.haltBug;
        eiPending_ = hdr.eiPending;
        ie_ = hdr.ie; iflag_ = hdr.iflag;
        lcdc_ = hdr.lcdc; lcdstat_ = hdr.lcdstat; scy_ = hdr.scy; scx_ = hdr.scx;
        ly_ = hdr.ly; lyc_ = hdr.lyc; bgp_ = hdr.bgp; obp0_ = hdr.obp0; obp1_ = hdr.obp1;
        wy_ = hdr.wy; wx_ = hdr.wx;
        tima_ = hdr.tima; tma_ = hdr.tma; tac_ = hdr.tac; divReg_ = hdr.divReg;
        timerCounter_ = hdr.timerCounter; divCounter_ = hdr.divCounter;
        romBank_ = hdr.romBank; ramBank_ = hdr.ramBank; ramEnabled_ = hdr.ramEnabled;
        joypadSelect_ = hdr.joypadSelect;
        mbc1Mode_ = hdr.mbc1Mode; mbc1Bank2_ = hdr.mbc1Bank2;
        refreshMappedRomBanks();
        ppuMode_ = hdr.ppuMode;
        prevStatLine_ = hdr.prevStatLine != 0;
        memcpy(soundRegs_, hdr.soundRegs, sizeof(soundRegs_));
        frameCount = hdr.frameCount;

        // v4: CGB state + serial registers.
        vramBank_ = hdr.vramBank;
        wramBank_ = hdr.wramBank;
        if (wramBank_ == 0) wramBank_ = 1;
        key1_ = hdr.key1;
        doubleSpeed_ = hdr.doubleSpeed != 0;
        bcpsReg_ = hdr.bcpsReg;
        ocpsReg_ = hdr.ocpsReg;
        memcpy(bgPalette_, hdr.bgPalette, sizeof(bgPalette_));
        memcpy(objPalette_, hdr.objPalette, sizeof(objPalette_));
        hdma1_ = hdr.hdma1; hdma2_ = hdr.hdma2; hdma3_ = hdr.hdma3;
        hdma4_ = hdr.hdma4; hdma5_ = hdr.hdma5;
        sbReg_ = hdr.sbReg; scReg_ = hdr.scReg;

        // Each read is bounded — a truncated save (mid-body torn write or
        // SD wear) would otherwise leave the trailing buffers with stale
        // data from the previous game / boot, and the emulator would run
        // on partially corrupted state. Detect any short read up front
        // and reject the save outright; loadROM's clean reset will run
        // instead, which is the same recovery path as a missing save.
        auto readExact = [&](void* dst, size_t n) -> bool {
            return static_cast<size_t>(f.read(dst, n)) == n;
        };
        if (!readExact(vram, GB_VRAM_SIZE) ||
            !readExact(wram, GB_WRAM_SIZE) ||
            !readExact(oam, sizeof(oam)) ||
            !readExact(hram, sizeof(hram)) ||
            !readExact(sramData, sramSize_)) {
            f.close();
            sd.remove(path);
            Serial.println("[GB] Save state body truncated, removed");
            return false;
        }
        // v4 trailer: only read if the save was from a CGB run AND the
        // current emulator has the extra buffers allocated. If the save
        // was DMG, nothing was written beyond sramData — done.
        if (hdr.isCgbSave && isCgb_ && vram1_) {
            if (!readExact(vram1_, 0x2000)) {
                f.close();
                sd.remove(path);
                Serial.println("[GB] CGB VRAM bank 1 truncated, removed");
                return false;
            }
        }
        if (hdr.isCgbSave && isCgb_ && wramExtra_) {
            if (!readExact(wramExtra_, 0x6000)) {
                f.close();
                sd.remove(path);
                Serial.println("[GB] CGB WRAM extra truncated, removed");
                return false;
            }
        }
        f.close();

        joyDpad_ = 0x0F; joyButtons_ = 0x0F;
        for (int i = 0; i < 2; i++) runFrame();
        return true;
    }

    bool saveSRAM(SdFat& sd, const char* path) override {
        char tmpPath[84];
        buildTmpPath(path, tmpPath, sizeof(tmpPath));
        FsFile f = sd.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC);
        if (!f) return false;
        f.write(sramData, sramSize_);
        // MBC3 RTC trailer (10 bytes): 5 live + 5 latched. Appended to every
        // SRAM save regardless of mapper — the overhead is 10 bytes and
        // loadSRAM tolerates old files with no trailer. This makes Pokemon
        // Gold/Silver/Crystal day-night cycles survive power cycles.
        uint8_t rtcBlob[10] = {
            rtcSec_, rtcMin_, rtcHour_, rtcDay_, rtcDayHi_,
            rtcLatched_[0], rtcLatched_[1], rtcLatched_[2], rtcLatched_[3], rtcLatched_[4]
        };
        f.write(rtcBlob, sizeof(rtcBlob));
        f.sync();
        f.close();
        return safeRename(sd, tmpPath, path);
    }

    bool loadSRAM(SdFat& sd, const char* path) override {
        if (!sd.exists(path)) return false;
        FsFile f = sd.open(path, O_RDONLY);
        if (!f) return false;
        // Accept any file >= GB_SRAM_SIZE. Legacy saves exactly match
        // SRAM size; v1.4+ saves have a 10-byte RTC trailer. Future trailer
        // extensions should only append, never insert.
        if ((int)f.size() < (int)sramSize_) { f.close(); return false; }
        f.read(sramData, sramSize_);
        if (f.available() >= 10) {
            uint8_t rtcBlob[10] = {};
            f.read(rtcBlob, sizeof(rtcBlob));
            rtcSec_    = rtcBlob[0];
            rtcMin_    = rtcBlob[1];
            rtcHour_   = rtcBlob[2];
            rtcDay_    = rtcBlob[3];
            rtcDayHi_  = rtcBlob[4];
            rtcLatched_[0] = rtcBlob[5];
            rtcLatched_[1] = rtcBlob[6];
            rtcLatched_[2] = rtcBlob[7];
            rtcLatched_[3] = rtcBlob[8];
            rtcLatched_[4] = rtcBlob[9];
        }
        f.close();
        return true;
    }

    // ====== BANK CACHE ======
    void setupBankCache(uint8_t* bank0, uint8_t* cache, int slots, int* map) {
        romBank0_ = bank0;
        bankCache_ = cache;
        cacheSlots_ = slots;
        cacheMap_ = map;
        cacheNextSlot_ = 0;
    }

    uint8_t readCachedBank(uint16_t bank, uint16_t offset) {
        // No cache slots — read directly from flash (slow but safe).
        // Note: cannot patch this path because we never have the full bank
        // in RAM. Patches in cache-bypassed banks will not apply, but the
        // common case (Pokemon Red on a 1MB device with 4-8 cache slots)
        // always uses the cached path.
        if (cacheSlots_ == 0) {
            uint8_t val;
            if (!romFile_.seek((uint32_t)bank * ROM_BANK_SIZE + offset) ||
                romFile_.read(&val, 1) != 1) {
                romIoFailed_ = true;
                return 0xFF;
            }
            return val;
        }
        for (int i = 0; i < cacheSlots_; i++) {
            if (cacheMap_[i] == (int)bank) {
                return bankCache_[i * ROM_BANK_SIZE + offset];
            }
        }
        int slot = cacheNextSlot_;
        cacheNextSlot_ = (cacheNextSlot_ + 1) % cacheSlots_;
        if (!romFile_.seek((uint32_t)bank * ROM_BANK_SIZE) ||
            romFile_.read(&bankCache_[slot * ROM_BANK_SIZE], ROM_BANK_SIZE) != ROM_BANK_SIZE) {
            romIoFailed_ = true;
            return 0xFF;
        }
        cacheMap_[slot] = bank;

        // Apply per-bank patches AFTER load. This is the only place patches
        // can apply for cached ROMs — bank 0 is patched separately at boot.
        // Cost: ~30 patches × 1 comparison each, only on a fresh load (not
        // cache hits), which is microseconds vs the millisecond flash read.
        if (pokemonRedPatch_) {
            pokered::patchBank(&bankCache_[slot * ROM_BANK_SIZE], bank);
        }
        if (gbPatchEnabled_ && romBank0_) {
            // Generalized framework: covers Tetris, Mario's Picross, Dr. Mario,
            // and any other games registered in gb_eink_patches.h. The framework
            // takes bank0 as its "identification source" so it can re-check the
            // title every call without trusting a cached flag.
            gbpatches::applyToBank(&bankCache_[slot * ROM_BANK_SIZE],
                                   bank, romBank0_, ROM_BANK_SIZE);
        }

        return bankCache_[slot * ROM_BANK_SIZE + offset];
    }

    // ====== MEMORY BUS ======
    // Cheat hook helper — wraps a value in a cheat lookup. Cheat list
    // empty in the common case so this is a single branch.
    inline uint8_t cheatWrap(uint16_t addr, uint8_t real) {
        if (cheats::g_activeCount == 0) return real;
        uint16_t cheated = cheats::lookup(SYS_GAMEBOY, addr, real);
        return (cheated != cheats::MISS) ? (uint8_t)cheated : real;
    }

    void refreshMappedRomBanks() {
        if (!romInRam_ || !romData_ || !romBankCount_) {
            mappedRomBanks_[0] = mappedRomBanks_[1] = nullptr;
            return;
        }
        uint32_t low = (mbcType_ == MBC_MBC1 && mbc1Mode_ == 1)
            ? (uint32_t)mbc1Bank2_ << 5 : 0;
        mappedRomBanks_[0] = romData_ + (low % romBankCount_) * ROM_BANK_SIZE;
        mappedRomBanks_[1] = romData_ + (romBank_ % romBankCount_) * ROM_BANK_SIZE;
    }

    uint8_t readByte(uint16_t addr) {
        // Mapped Flash: resolve banking when the mapper changes, not on
        // every opcode/operand read. Streaming ROMs retain their cache path.
        if (addr < 0x8000 && mappedRomBanks_[addr >> 14])
            return cheatWrap(addr, mappedRomBanks_[addr >> 14][addr & 0x3fff]);
        // ROM Bank 0 (fixed) — final return wrapped in cheat lookup so
        // Game Genie codes for low-address ROM bytes apply transparently.
        if (addr < 0x4000) {
            uint8_t real;
            if (mbcType_ == MBC_MBC1 && mbc1Mode_ == 1) {
                // MBC1 mode 1: bank 0 area uses upper bits
                uint32_t effectiveBank = (uint32_t)mbc1Bank2_ << 5;
                if (effectiveBank >= romBankCount_) effectiveBank %= romBankCount_;
                if (romInRam_) real = romData_[effectiveBank * ROM_BANK_SIZE + addr];
                else if (effectiveBank == 0) real = romBank0_[addr];
                else real = readCachedBank(effectiveBank, addr);
            } else {
                real = romInRam_ ? romData_[addr] : romBank0_[addr];
            }
            return cheatWrap(addr, real);
        }

        // ROM Bank N (switchable) — wrapped in cheat lookup as well
        if (addr < 0x8000) {
            uint16_t bank = romBank_;
            if (bank >= romBankCount_) bank = bank % romBankCount_;
            uint8_t real = romInRam_
                ? romData_[(uint32_t)bank * ROM_BANK_SIZE + (addr - 0x4000)]
                : readCachedBank(bank, addr - 0x4000);
            return cheatWrap(addr, real);
        }

        // VRAM — bank 0 is always vram[]; bank 1 lives in vram1_ (CGB only)
        if (addr < 0xA000) {
            if (isCgb_ && vramBank_ && vram1_) return vram1_[addr - 0x8000];
            return vram[addr - 0x8000];
        }

        // External RAM
        if (addr < 0xC000) {
            if (!ramEnabled_) return 0xFF;
            // MBC2 has 512×4-bit RAM; only the lower nibble of each byte
            // is meaningful. Mirror the 512 bytes across the entire
            // $A000-$BFFF window like real hardware.
            if (mbcType_ == MBC_MBC2) {
                return 0xF0 | (sramData[(addr - 0xA000) & 0x1FF] & 0x0F);
            }
            // MBC3 RTC registers: ramBank_ 0x08-0x0C maps to RTC seconds/min/hour/day-low/day-high
            if (mbcType_ == MBC_MBC3 && ramBank_ >= 0x08 && ramBank_ <= 0x0C) {
                tickRTC();
                int idx = ramBank_ - 0x08;
                return rtcLatched_[idx];
            }
            if (ramBank_ <= 3) {
                const uint32_t idx = (uint32_t)ramBank_ * RAM_BANK_SIZE + (addr - 0xA000);
                // sramData is sized GB_SRAM_MAX — clamp so cartridges that
                // advertise >8 KB SRAM don't stomp the heap.
                if (idx >= sramPersistSize_) return 0xFF;
                return sramData[idx];
            }
            return 0xFF;
        }

        // WRAM — bank 0 fixed at C000-CFFF, bank N at D000-DFFF
        // CGB: banks 1-7 selectable via SVBK ($FF70). Bank 1 lives in wram[]
        // (existing 8KB array). Banks 2-7 live in wramExtra_ (24KB heap).
        if (addr < 0xD000) return wram[addr - 0xC000];
        if (addr < 0xE000) {
            if (isCgb_ && wramBank_ >= 2 && wramExtra_) {
                // Banks 2-7 in heap buffer; each bank is 4 KB
                return wramExtra_[(wramBank_ - 2) * 0x1000 + (addr - 0xD000)];
            }
            return wram[addr - 0xC000];  // Bank 1 in DMG-fashion
        }

        // Echo RAM (mirror of C000-DDFF). On CGB the F000-FDFF half of
        // echo RAM mirrors the banked WRAM ($D000-$DDFF), so we need to
        // go through the same bank-select path as the canonical range.
        if (addr < 0xFE00) {
            uint16_t mirror = addr - 0x2000;  // Maps to C000-DDFF
            if (mirror < 0xD000) return wram[mirror - 0xC000];
            if (isCgb_ && wramBank_ >= 2 && wramExtra_) {
                return wramExtra_[(wramBank_ - 2) * 0x1000 + (mirror - 0xD000)];
            }
            return wram[mirror - 0xC000];
        }

        // OAM
        if (addr < 0xFEA0) return oam[addr - 0xFE00];

        // Unusable
        if (addr < 0xFF00) return 0xFF;

        // IO Registers
        switch (addr) {
            case 0xFF00: {
                uint8_t result = 0xCF;
                if (!(joypadSelect_ & 0x10)) result &= (joyDpad_ | 0xF0);
                if (!(joypadSelect_ & 0x20)) result &= (joyButtons_ | 0xF0);
                return result;
            }
            case 0xFF01: return sbReg_;
            case 0xFF02: return scReg_ | 0x7E;  // Unused bits read as 1
            case 0xFF04: return divReg_;
            case 0xFF05: return tima_;
            case 0xFF06: return tma_;
            case 0xFF07: return tac_ | 0xF8;
            case 0xFF0F: return iflag_ | 0xE0;

            case 0xFF10: case 0xFF11: case 0xFF12: case 0xFF13: case 0xFF14:
            case 0xFF16: case 0xFF17: case 0xFF18: case 0xFF19:
            case 0xFF1A: case 0xFF1B: case 0xFF1C: case 0xFF1D: case 0xFF1E:
            case 0xFF20: case 0xFF21: case 0xFF22: case 0xFF23:
            case 0xFF24: case 0xFF25: case 0xFF26:
                return soundRead_ ? soundRead_(soundContext_, addr) : soundRegs_[addr - 0xFF10];

            case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
            case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
            case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
            case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
                return soundRead_ ? soundRead_(soundContext_, addr) : soundRegs_[addr - 0xFF10];

            case 0xFF40: return lcdc_;
            case 0xFF41: return lcdstat_ | 0x80 | ((ly_ == lyc_) ? 4 : 0) | (ppuMode_ & 3);
            case 0xFF42: return scy_;
            case 0xFF43: return scx_;
            case 0xFF44:
                // Real hardware pins LY to 0 while the LCD is off (LCDC bit 7
                // clear). Games that poll LY during a vblank window in the
                // middle of disabling the LCD will spin forever if we return
                // a non-zero value, so we explicitly report 0 in that case.
                return (lcdc_ & 0x80) ? ly_ : 0;
            case 0xFF45: return lyc_;
            case 0xFF47: return bgp_;
            case 0xFF48: return obp0_;
            case 0xFF49: return obp1_;
            case 0xFF4A: return wy_;
            case 0xFF4B: return wx_;
            // ── CGB registers — return 0xFF on DMG ROMs ──
            case 0xFF4D: return isCgb_ ? ((doubleSpeed_ ? 0x80 : 0) | (key1_ & 0x01) | 0x7E) : 0xFF;
            case 0xFF4F: return isCgb_ ? (vramBank_ | 0xFE) : 0xFF;
            case 0xFF51: return isCgb_ ? hdma1_ : 0xFF;
            case 0xFF52: return isCgb_ ? hdma2_ : 0xFF;
            case 0xFF53: return isCgb_ ? hdma3_ : 0xFF;
            case 0xFF54: return isCgb_ ? hdma4_ : 0xFF;
            case 0xFF55: return isCgb_ ? hdma5_ : 0xFF;
            case 0xFF68: return isCgb_ ? bcpsReg_ : 0xFF;
            case 0xFF69: return isCgb_ ? bgPalette_[bcpsReg_ & 0x3F] : 0xFF;
            case 0xFF6A: return isCgb_ ? ocpsReg_ : 0xFF;
            case 0xFF6B: return isCgb_ ? objPalette_[ocpsReg_ & 0x3F] : 0xFF;
            case 0xFF70: return isCgb_ ? (wramBank_ | 0xF8) : 0xFF;
            case 0xFFFF: return ie_;
        }

        if (addr >= 0xFF80 && addr < 0xFFFF)
            return hram[addr - 0xFF80];

        return 0xFF;
    }

    void writeByte(uint16_t addr, uint8_t val) {
        // MBC register writes (0x0000-0x7FFF)
        if (addr < 0x8000) {
            writeMBC(addr, val);
            return;
        }

        // VRAM — bank-aware on CGB
        if (addr < 0xA000) {
            if (isCgb_ && vramBank_ && vram1_) vram1_[addr - 0x8000] = val;
            else vram[addr - 0x8000] = val;
            return;
        }

        // External RAM
        if (addr < 0xC000) {
            if (!ramEnabled_) return;
            // MBC2: 512×4-bit RAM mirrored across the entire window
            if (mbcType_ == MBC_MBC2) {
                const uint32_t idx = (addr - 0xA000) & 0x1FF;
                const uint8_t stored = val & 0x0F;
                if (sramData[idx] != stored) {
                    sramData[idx] = stored;
                    sramDirty_ = true;
                }
                return;
            }
            // MBC3 RTC register writes — when ramBank is 0x08-0x0C the
            // $A000-$BFFF window writes the corresponding RTC register.
            // Games use this to set the clock (e.g. for time calibration)
            // or to correct the carry/halt flags. Per spec the game should
            // halt the RTC before writing to avoid mid-tick updates; we
            // don't enforce that but respect the halt bit going forward.
            if (mbcType_ == MBC_MBC3 && ramBank_ >= 0x08 && ramBank_ <= 0x0C) {
                switch (ramBank_) {
                    case 0x08: rtcSec_ = val & 0x3F; break;   // 0..59
                    case 0x09: rtcMin_ = val & 0x3F; break;   // 0..59
                    case 0x0A: rtcHour_ = val & 0x1F; break;  // 0..23
                    case 0x0B: rtcDay_ = val; break;           // day low (0..255)
                    case 0x0C:
                        // day-high: bit 0 = day bit 8, bit 6 = halt, bit 7 = carry
                        rtcDayHi_ = val & 0xC1;
                        break;
                }
                return;
            }
            if (ramBank_ <= 3) {
                const uint32_t idx = (uint32_t)ramBank_ * RAM_BANK_SIZE + (addr - 0xA000);
                // Matches the read-side clamp — GB_SRAM_MAX bounds.
                if (idx < sramPersistSize_ && sramData[idx] != val) {
                    sramData[idx] = val;
                    sramDirty_ = true;
                }
            }
            return;
        }

        // WRAM — bank-aware on CGB
        if (addr < 0xD000) { wram[addr - 0xC000] = val; return; }
        if (addr < 0xE000) {
            if (isCgb_ && wramBank_ >= 2 && wramExtra_) {
                wramExtra_[(wramBank_ - 2) * 0x1000 + (addr - 0xD000)] = val;
            } else {
                wram[addr - 0xC000] = val;
            }
            return;
        }
        if (addr < 0xFE00) {
            // Echo RAM write — route through the same CGB banked-WRAM path
            // as the main WRAM range, so writes to the F000-FDFF half of
            // the echo go to the currently-paged bank instead of always
            // landing in bank 1. Matches hardware behavior.
            uint16_t mirror = addr - 0x2000;
            if (mirror < 0xD000) { wram[mirror - 0xC000] = val; return; }
            if (isCgb_ && wramBank_ >= 2 && wramExtra_) {
                wramExtra_[(wramBank_ - 2) * 0x1000 + (mirror - 0xD000)] = val;
            } else {
                wram[mirror - 0xC000] = val;
            }
            return;
        }
        if (addr < 0xFEA0) { oam[addr - 0xFE00] = val; return; }
        if (addr < 0xFF00) return;

        // IO Registers
        switch (addr) {
            case 0xFF00: joypadSelect_ = val & 0x30; return;
            case 0xFF01: sbReg_ = val; return;  // Serial buffer
            case 0xFF02:
                // Serial control ($FF02). Bit 7 = transfer start, bit 0 =
                // clock source (1 = internal). We don't emulate the link
                // cable or printer, but games that initiate a transfer will
                // busy-wait for the serial interrupt (bit 3 of IF) — so we
                // immediately mark the transfer complete and fire the IRQ.
                // Reads of $FF01 return 0xFF afterward (no connected peer).
                scReg_ = val & 0x83;
                if (val & 0x80) {
                    // "Transfer complete" — clear start bit, raise IRQ
                    scReg_ &= 0x7F;
                    sbReg_ = 0xFF;
                    iflag_ |= 0x08;
                }
                return;
            case 0xFF04: divReg_ = 0; divCounter_ = 0; return;
            case 0xFF05: tima_ = val; return;
            case 0xFF06: tma_ = val; return;
            case 0xFF07: tac_ = val & 0x07; return;
            case 0xFF0F: iflag_ = val & 0x1F; return;

            case 0xFF10: case 0xFF11: case 0xFF12: case 0xFF13: case 0xFF14:
            case 0xFF16: case 0xFF17: case 0xFF18: case 0xFF19:
            case 0xFF1A: case 0xFF1B: case 0xFF1C: case 0xFF1D: case 0xFF1E:
            case 0xFF20: case 0xFF21: case 0xFF22: case 0xFF23:
            case 0xFF24: case 0xFF25: case 0xFF26:
                soundRegs_[addr - 0xFF10] = val;
                if (soundWrite_) soundWrite_(soundContext_, addr, val);
                return;
            case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33:
            case 0xFF34: case 0xFF35: case 0xFF36: case 0xFF37:
            case 0xFF38: case 0xFF39: case 0xFF3A: case 0xFF3B:
            case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
                soundRegs_[addr - 0xFF10] = val;
                if (soundWrite_) soundWrite_(soundContext_, addr, val);
                return;

            case 0xFF40: {
                // When the LCD is turned off (bit 7 → 0), LY resets to 0,
                // the PPU clock resets, and mode becomes 0. Games that
                // disable the LCD to reconfigure VRAM need this behavior
                // — without it, LY reports whatever line it was on when
                // the LCD went off, which can cause the LCD-on transition
                // to fire STAT interrupts on the wrong scanline.
                bool wasOn = (lcdc_ & 0x80) != 0;
                bool nowOn = (val & 0x80) != 0;
                lcdc_ = val;
                if (wasOn && !nowOn) {
                    ly_ = 0;
                    ppuMode_ = 0;
                    prevStatLine_ = false;
                    windowLine_ = 0;
                }
                return;
            }
            case 0xFF41:
                // LCDSTAT write preserves the low 3 bits (mode + LYC match)
                // and updates the source-enable bits (3-6). Writing can
                // immediately fire a STAT interrupt if one of the newly
                // enabled sources is currently active.
                lcdstat_ = (lcdstat_ & 0x07) | (val & 0x78);
                checkStatInterrupt();
                return;
            case 0xFF42: scy_ = val; return;
            case 0xFF43: scx_ = val; return;
            case 0xFF44: return;
            case 0xFF45:
                // LYC write can immediately trigger a STAT interrupt if
                // the new LYC matches the current LY (and the LYC-match
                // source enable in LCDSTAT is set). We call the shared
                // STAT check helper so the rising-edge logic stays in
                // one place. Games that update LYC mid-frame to chain
                // split raster effects depend on this.
                lyc_ = val;
                checkStatInterrupt();
                return;
            case 0xFF46:
                for (int i = 0; i < 0xA0; i++) oam[i] = readByte((val << 8) | i);
                return;
            case 0xFF47: bgp_ = val; return;
            case 0xFF48: obp0_ = val; return;
            case 0xFF49: obp1_ = val; return;
            case 0xFF4A: wy_ = val; return;
            case 0xFF4B: wx_ = val; return;
            // ── CGB registers (only effective when isCgb_) ──
            case 0xFF4D:  // KEY1: speed switch — bit 0 sets prepare, STOP triggers
                if (isCgb_) key1_ = (key1_ & 0x80) | (val & 0x01);
                return;
            case 0xFF4F:  // VBK: VRAM bank select (bit 0 only)
                if (isCgb_) vramBank_ = val & 0x01;
                return;
            case 0xFF51: if (isCgb_) hdma1_ = val; return;
            case 0xFF52: if (isCgb_) hdma2_ = val & 0xF0; return;
            case 0xFF53: if (isCgb_) hdma3_ = val & 0x1F; return;
            case 0xFF54: if (isCgb_) hdma4_ = val & 0xF0; return;
            case 0xFF55:  // HDMA5: triggers transfer (we do general DMA immediately)
                if (isCgb_) {
                    // Source must be from ROM ($0000-$7FF0), WRAM ($C000-$DFF0),
                    // or cartridge SRAM ($A000-$DFF0). Destination must be in
                    // VRAM ($8000-$9FF0). Per the CGB spec, the source low 4
                    // bits and destination low 4 bits are ignored (16-byte
                    // aligned). Length is (N+1)*16 where N is bits 0-6 of val.
                    uint16_t src = ((hdma1_ << 8) | hdma2_) & 0xFFF0;
                    uint16_t dst = (0x8000 | (hdma3_ << 8) | hdma4_) & 0x9FF0;
                    int len = ((val & 0x7F) + 1) * 16;
                    // Reject transfers with invalid source ranges ($E000+).
                    // Games rarely hit this but homebrew might.
                    if (src >= 0xE000 || (src >= 0x8000 && src < 0xA000)) {
                        hdma5_ = 0x80 | (val & 0x7F);  // Report "not started"
                        return;
                    }
                    // We don't distinguish HDMA (HBlank-paced) from GDMA
                    // (immediate). Both copy len bytes from src→VRAM[dst].
                    // HBlank-paced games may show seams; most games are fine.
                    for (int i = 0; i < len && (dst + i) < 0xA000; i++) {
                        writeByte(dst + i, readByte(src + i));
                    }
                    hdma5_ = 0xFF;  // Transfer complete
                }
                return;
            case 0xFF68:  // BCPS: BG palette index + auto-increment
                if (isCgb_) bcpsReg_ = val;
                return;
            case 0xFF69:  // BCPD: BG palette data
                if (isCgb_) {
                    bgPalette_[bcpsReg_ & 0x3F] = val;
                    if (bcpsReg_ & 0x80) bcpsReg_ = 0x80 | ((bcpsReg_ + 1) & 0x3F);
                }
                return;
            case 0xFF6A:
                if (isCgb_) ocpsReg_ = val;
                return;
            case 0xFF6B:
                if (isCgb_) {
                    objPalette_[ocpsReg_ & 0x3F] = val;
                    if (ocpsReg_ & 0x80) ocpsReg_ = 0x80 | ((ocpsReg_ + 1) & 0x3F);
                }
                return;
            case 0xFF70:  // SVBK: WRAM bank (1-7; 0 maps to 1)
                if (isCgb_) {
                    wramBank_ = val & 0x07;
                    if (wramBank_ == 0) wramBank_ = 1;
                }
                return;
            case 0xFFFF: ie_ = val; return;
        }

        if (addr >= 0xFF80 && addr < 0xFFFF)
            hram[addr - 0xFF80] = val;
    }

    // ====== MBC WRITE HANDLER — supports None/MBC1/MBC3/MBC5 ======
    void writeMBC(uint16_t addr, uint8_t val) {
        switch (mbcType_) {
        case MBC_NONE:
            // ROM-only cartridge — no banking, ignore writes
            break;

        case MBC_MBC1:
            if (addr < 0x2000) {
                ramEnabled_ = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                // 5-bit ROM bank register — 0 maps to 1
                uint8_t bank = val & 0x1F;
                if (bank == 0) bank = 1;
                romBank_ = (mbc1Bank2_ << 5) | bank;
            } else if (addr < 0x6000) {
                // 2-bit register: RAM bank or upper ROM bits
                mbc1Bank2_ = val & 0x03;
                if (mbc1Mode_ == 0) {
                    // ROM banking mode: affects upper bits of ROM bank
                    romBank_ = (mbc1Bank2_ << 5) | (romBank_ & 0x1F);
                } else {
                    // RAM banking mode: selects RAM bank
                    ramBank_ = mbc1Bank2_;
                }
            } else {
                // Mode select: 0=ROM banking, 1=RAM banking
                mbc1Mode_ = val & 0x01;
                if (mbc1Mode_ == 0) ramBank_ = 0;
            }
            break;

        case MBC_MBC2:
            // MBC2 has only 16 ROM banks (max 256 KB) and 512×4-bit RAM.
            // The cart has a single register region — addr bit 8 (0x100)
            // distinguishes RAM-enable writes from ROM-bank writes.
            if (addr < 0x4000) {
                if (addr & 0x0100) {
                    // ROM bank select (low 4 bits, can't be 0)
                    romBank_ = val & 0x0F;
                    if (romBank_ == 0) romBank_ = 1;
                } else {
                    ramEnabled_ = ((val & 0x0F) == 0x0A);
                }
            }
            break;

        case MBC_MBC3:
            if (addr < 0x2000) {
                ramEnabled_ = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                romBank_ = val & 0x7F;
                if (romBank_ == 0) romBank_ = 1;
            } else if (addr < 0x6000) {
                ramBank_ = val;
            } else {
                // RTC latch: 0 followed by 1 latches the current RTC
                // values into the readable registers. Pokemon Crystal
                // does this every time it reads the time.
                if (val == 0x01 && rtcLatchPrev_ == false) {
                    tickRTC();
                    rtcLatched_[0] = rtcSec_;
                    rtcLatched_[1] = rtcMin_;
                    rtcLatched_[2] = rtcHour_;
                    rtcLatched_[3] = rtcDay_;
                    rtcLatched_[4] = rtcDayHi_;
                }
                rtcLatchPrev_ = (val == 0x00);
            }
            break;

        case MBC_MBC5:
            if (addr < 0x2000) {
                ramEnabled_ = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x3000) {
                // Low 8 bits of ROM bank
                romBank_ = (romBank_ & 0x100) | val;
            } else if (addr < 0x4000) {
                // Bit 8 of ROM bank (9-bit total)
                romBank_ = (romBank_ & 0xFF) | ((val & 0x01) << 8);
            } else if (addr < 0x6000) {
                ramBank_ = val & 0x0F;
            }
            break;
        }
        refreshMappedRomBanks();
    }

    // ====== TIMER ======
    void updateTimer(int cycles) {
        divCounter_ += cycles;
        while (divCounter_ >= 256) {
            divCounter_ -= 256;
            divReg_++;
        }
        if (!(tac_ & 0x04)) return;
        // Timer frequency table — kept as local array; compiler places in rodata
        static const DRAM_ATTR uint16_t freqs[] = {1024, 16, 64, 256};
        uint16_t freq = freqs[tac_ & 0x03];
        timerCounter_ += cycles;
        while (timerCounter_ >= freq) {
            timerCounter_ -= freq;
            tima_++;
            if (tima_ == 0) {
                tima_ = tma_;
                iflag_ |= 0x04;
            }
        }
    }

    // ====== CPU T-cycle tables (SM83) ======
    // Base opcode T-cycles (conditional branches use NOT-TAKEN cost; caller adds extra)
    static const uint8_t opCycles_[256];
    // CB-prefixed opcode T-cycles (all 8 for reg ops, 16 for (HL) except BIT which is 12)
    static const uint8_t cbCycles_[256];

    // ====== CPU ======
    uint16_t fetch16() {
        uint8_t low = readByte(pc_++);
        uint8_t high = readByte(pc_++);
        return (uint16_t)low | ((uint16_t)high << 8);
    }

    int cpu_step(int idleBudget = 456) {
        // EI delay semantics: the instruction immediately after EI must
        // execute BEFORE interrupts can fire. By checking interrupts first
        // using the PRE-EI ime_ state, and then promoting eiPending_ to
        // ime_ AFTER, we get the one-instruction delay correctly.
        //
        // Order:
        //   1. Check interrupts with current ime_
        //   2. Promote eiPending_ (so ime_ takes effect for the NEXT step)
        //   3. Fetch + execute this step's instruction
        //
        // This fix matters for games that use EI immediately before a
        // time-sensitive loop — wrong ordering can cause the first
        // iteration to be lost to an unexpected interrupt dispatch.
        uint8_t pending = ie_ & iflag_ & 0x1F;
        if (pending) {
            halted_ = false;
            if (ime_) {
                ime_ = false;
                sp_ -= 2;
                writeByte(sp_, pc_ & 0xFF);
                writeByte(sp_ + 1, pc_ >> 8);
                if      (pending & 0x01) { iflag_ &= ~0x01; pc_ = 0x40; }
                else if (pending & 0x02) { iflag_ &= ~0x02; pc_ = 0x48; }
                else if (pending & 0x04) { iflag_ &= ~0x04; pc_ = 0x50; }
                else if (pending & 0x08) { iflag_ &= ~0x08; pc_ = 0x58; }
                else if (pending & 0x10) { iflag_ &= ~0x10; pc_ = 0x60; }
                return 20;  // Interrupt dispatch: 5 M-cycles = 20 T-cycles
            }
        }

        if (halted_) {
            // HALT fast-forward: skip to next potential interrupt source
            // instead of spinning 4 T-cycles at a time (114 iterations/scanline)
            int skip = idleBudget;  // Do not skip the next PPU phase/interrupt.

            // Timer interrupt: if timer AND timer IE bit both enabled
            if ((tac_ & 0x04) && (ie_ & 0x04)) {
                static const uint16_t freqs[] = {1024, 16, 64, 256};
                uint16_t freq = freqs[tac_ & 3];
                // Cycles until TIMA overflows to 0
                int toOverflow = (int)(256 - tima_) * freq - timerCounter_;
                if (toOverflow > 0 && toOverflow < skip) skip = toOverflow;
            }

            return skip < 4 ? 4 : skip;
        }

        // Promote pending EI → IME active. This happens AFTER the interrupt
        // check above but BEFORE the instruction we're about to execute, so
        // the instruction itself runs with interrupts masked; any interrupt
        // that fires will be serviced at the START of the next cpu_step().
        if (eiPending_) { eiPending_ = false; ime_ = true; }

        uint8_t op = readByte(pc_++);
        if (haltBug_) { pc_--; haltBug_ = false; }
        int branchExtra = 0;  // Extra T-cycles for taken conditional branches

        switch (op) {
            case 0x00: break;
            case 0x01: c_ = readByte(pc_++); b_ = readByte(pc_++); break;
            case 0x02: writeByte((b_ << 8) | c_, a_); break;
            case 0x03: { uint16_t bc = ((b_ << 8) | c_) + 1; b_ = bc >> 8; c_ = bc & 0xFF; } break;
            case 0x04: b_++; f_ = (f_ & 0x10) | (b_ ? 0 : 0x80) | ((b_ & 0x0F) ? 0 : 0x20); break;
            case 0x05: b_--; f_ = (f_ & 0x10) | 0x40 | (b_ ? 0 : 0x80) | ((b_ & 0x0F) == 0x0F ? 0x20 : 0); break;
            case 0x06: b_ = readByte(pc_++); break;
            case 0x07: { uint8_t c7 = a_ >> 7; a_ = (a_ << 1) | c7; f_ = c7 ? 0x10 : 0; } break;
            case 0x08: { uint16_t ad = fetch16(); writeByte(ad, sp_ & 0xFF); writeByte(ad + 1, sp_ >> 8); } break;
            case 0x09: { uint16_t hl = (h_ << 8) | l_; uint16_t bc = (b_ << 8) | c_; uint32_t r = hl + bc; f_ = (f_ & 0x80) | ((r > 0xFFFF) ? 0x10 : 0) | (((hl & 0xFFF) + (bc & 0xFFF) > 0xFFF) ? 0x20 : 0); h_ = (r >> 8) & 0xFF; l_ = r & 0xFF; } break;
            case 0x0A: a_ = readByte((b_ << 8) | c_); break;
            case 0x0B: { uint16_t bc = ((b_ << 8) | c_) - 1; b_ = bc >> 8; c_ = bc & 0xFF; } break;
            case 0x0C: c_++; f_ = (f_ & 0x10) | (c_ ? 0 : 0x80) | ((c_ & 0x0F) ? 0 : 0x20); break;
            case 0x0D: c_--; f_ = (f_ & 0x10) | 0x40 | (c_ ? 0 : 0x80) | ((c_ & 0x0F) == 0x0F ? 0x20 : 0); break;
            case 0x0E: c_ = readByte(pc_++); break;
            case 0x0F: { uint8_t c0 = a_ & 1; a_ = (a_ >> 1) | (c0 << 7); f_ = c0 ? 0x10 : 0; } break;
            case 0x10:
                // STOP — normally waits for joypad interrupt on DMG. On CGB
                // with KEY1 bit 0 (prepare) set, STOP triggers the speed
                // switch: bit 7 (current speed) toggles and bit 0 clears.
                // Games that never request a switch just see a 2-byte NOP.
                //
                // We don't model cycle-accurate timing, so the "speed" flag
                // is a hint for logic that keys off it (e.g. the timer code)
                // rather than an actual clock doubling. Still, setting the
                // flag matters for CGB games (Oracle of Seasons/Ages) that
                // poll KEY1 to confirm the switch completed before reading
                // timers or doing double-buffer VRAM DMA.
                pc_++;
                if (isCgb_ && (key1_ & 0x01)) {
                    doubleSpeed_ = !doubleSpeed_;
                    key1_ = doubleSpeed_ ? 0x80 : 0x00;
                }
                break;
            case 0x11: e_ = readByte(pc_++); d_ = readByte(pc_++); break;
            case 0x12: writeByte((d_ << 8) | e_, a_); break;
            case 0x13: { uint16_t de = ((d_ << 8) | e_) + 1; d_ = de >> 8; e_ = de & 0xFF; } break;
            case 0x14: d_++; f_ = (f_ & 0x10) | (d_ ? 0 : 0x80) | ((d_ & 0x0F) ? 0 : 0x20); break;
            case 0x15: d_--; f_ = (f_ & 0x10) | 0x40 | (d_ ? 0 : 0x80) | ((d_ & 0x0F) == 0x0F ? 0x20 : 0); break;
            case 0x16: d_ = readByte(pc_++); break;
            case 0x17: { uint8_t c7 = a_ >> 7; a_ = (a_ << 1) | ((f_ >> 4) & 1); f_ = c7 ? 0x10 : 0; } break;
            case 0x18: pc_ += (int8_t)readByte(pc_) + 1; break;
            case 0x19: { uint16_t hl = (h_ << 8) | l_; uint16_t de = (d_ << 8) | e_; uint32_t r = hl + de; f_ = (f_ & 0x80) | ((r > 0xFFFF) ? 0x10 : 0) | (((hl & 0xFFF) + (de & 0xFFF) > 0xFFF) ? 0x20 : 0); h_ = (r >> 8) & 0xFF; l_ = r & 0xFF; } break;
            case 0x1A: a_ = readByte((d_ << 8) | e_); break;
            case 0x1B: { uint16_t de = ((d_ << 8) | e_) - 1; d_ = de >> 8; e_ = de & 0xFF; } break;
            case 0x1C: e_++; f_ = (f_ & 0x10) | (e_ ? 0 : 0x80) | ((e_ & 0x0F) ? 0 : 0x20); break;
            case 0x1D: e_--; f_ = (f_ & 0x10) | 0x40 | (e_ ? 0 : 0x80) | ((e_ & 0x0F) == 0x0F ? 0x20 : 0); break;
            case 0x1E: e_ = readByte(pc_++); break;
            case 0x1F: { uint8_t c0 = a_ & 1; a_ = (a_ >> 1) | ((f_ & 0x10) << 3); f_ = c0 ? 0x10 : 0; } break;
            case 0x20: if (!(f_ & 0x80)) { pc_ += (int8_t)readByte(pc_) + 1; branchExtra = 4; } else pc_++; break;
            case 0x21: l_ = readByte(pc_++); h_ = readByte(pc_++); break;
            case 0x22: writeByte((h_ << 8) | l_, a_); l_++; if (!l_) h_++; break;
            case 0x23: { uint16_t hl = ((h_ << 8) | l_) + 1; h_ = hl >> 8; l_ = hl & 0xFF; } break;
            case 0x24: h_++; f_ = (f_ & 0x10) | (h_ ? 0 : 0x80) | ((h_ & 0x0F) ? 0 : 0x20); break;
            case 0x25: h_--; f_ = (f_ & 0x10) | 0x40 | (h_ ? 0 : 0x80) | ((h_ & 0x0F) == 0x0F ? 0x20 : 0); break;
            case 0x26: h_ = readByte(pc_++); break;
            case 0x27: { int adj = 0; if ((f_ & 0x20) || (!(f_ & 0x40) && (a_ & 0x0F) > 9)) adj |= 0x06; if ((f_ & 0x10) || (!(f_ & 0x40) && a_ > 0x99)) { adj |= 0x60; f_ |= 0x10; } a_ += (f_ & 0x40) ? -adj : adj; f_ = (f_ & 0x50) | (a_ ? 0 : 0x80); } break;
            case 0x28: if (f_ & 0x80) { pc_ += (int8_t)readByte(pc_) + 1; branchExtra = 4; } else pc_++; break;
            case 0x29: { uint16_t hl = (h_ << 8) | l_; uint32_t r = hl + hl; f_ = (f_ & 0x80) | ((r > 0xFFFF) ? 0x10 : 0) | (((hl & 0xFFF) + (hl & 0xFFF) > 0xFFF) ? 0x20 : 0); h_ = (r >> 8) & 0xFF; l_ = r & 0xFF; } break;
            case 0x2A: a_ = readByte((h_ << 8) | l_); l_++; if (!l_) h_++; break;
            case 0x2B: { uint16_t hl = ((h_ << 8) | l_) - 1; h_ = hl >> 8; l_ = hl & 0xFF; } break;
            case 0x2C: l_++; f_ = (f_ & 0x10) | (l_ ? 0 : 0x80) | ((l_ & 0x0F) ? 0 : 0x20); break;
            case 0x2D: l_--; f_ = (f_ & 0x10) | 0x40 | (l_ ? 0 : 0x80) | ((l_ & 0x0F) == 0x0F ? 0x20 : 0); break;
            case 0x2E: l_ = readByte(pc_++); break;
            case 0x2F: a_ = ~a_; f_ |= 0x60; break;
            case 0x30: if (!(f_ & 0x10)) { pc_ += (int8_t)readByte(pc_) + 1; branchExtra = 4; } else pc_++; break;
            case 0x31: sp_ = fetch16(); break;
            case 0x32: writeByte((h_ << 8) | l_, a_); l_--; if (l_ == 0xFF) h_--; break;
            case 0x33: sp_++; break;
            case 0x34: { uint16_t ad = (h_ << 8) | l_; uint8_t v = readByte(ad) + 1; writeByte(ad, v); f_ = (f_ & 0x10) | (v ? 0 : 0x80) | ((v & 0x0F) ? 0 : 0x20); } break;
            case 0x35: { uint16_t ad = (h_ << 8) | l_; uint8_t v = readByte(ad) - 1; writeByte(ad, v); f_ = (f_ & 0x10) | 0x40 | (v ? 0 : 0x80) | ((v & 0x0F) == 0x0F ? 0x20 : 0); } break;
            case 0x36: writeByte((h_ << 8) | l_, readByte(pc_++)); break;
            case 0x37: f_ = (f_ & 0x80) | 0x10; break;
            case 0x38: if (f_ & 0x10) { pc_ += (int8_t)readByte(pc_) + 1; branchExtra = 4; } else pc_++; break;
            case 0x39: { uint16_t hl = (h_ << 8) | l_; uint32_t r = hl + sp_; f_ = (f_ & 0x80) | ((r > 0xFFFF) ? 0x10 : 0) | (((hl & 0xFFF) + (sp_ & 0xFFF) > 0xFFF) ? 0x20 : 0); h_ = (r >> 8) & 0xFF; l_ = r & 0xFF; } break;
            case 0x3A: a_ = readByte((h_ << 8) | l_); l_--; if (l_ == 0xFF) h_--; break;
            case 0x3B: sp_--; break;
            case 0x3C: a_++; f_ = (f_ & 0x10) | (a_ ? 0 : 0x80) | ((a_ & 0x0F) ? 0 : 0x20); break;
            case 0x3D: a_--; f_ = (f_ & 0x10) | 0x40 | (a_ ? 0 : 0x80) | ((a_ & 0x0F) == 0x0F ? 0x20 : 0); break;
            case 0x3E: a_ = readByte(pc_++); break;
            case 0x3F: f_ = (f_ & 0x80) | ((f_ & 0x10) ? 0 : 0x10); break;

            // LD r,r (0x40-0x7F)
            case 0x40: break; case 0x41: b_=c_; break; case 0x42: b_=d_; break; case 0x43: b_=e_; break;
            case 0x44: b_=h_; break; case 0x45: b_=l_; break; case 0x46: b_=readByte((h_<<8)|l_); break; case 0x47: b_=a_; break;
            case 0x48: c_=b_; break; case 0x49: break; case 0x4A: c_=d_; break; case 0x4B: c_=e_; break;
            case 0x4C: c_=h_; break; case 0x4D: c_=l_; break; case 0x4E: c_=readByte((h_<<8)|l_); break; case 0x4F: c_=a_; break;
            case 0x50: d_=b_; break; case 0x51: d_=c_; break; case 0x52: break; case 0x53: d_=e_; break;
            case 0x54: d_=h_; break; case 0x55: d_=l_; break; case 0x56: d_=readByte((h_<<8)|l_); break; case 0x57: d_=a_; break;
            case 0x58: e_=b_; break; case 0x59: e_=c_; break; case 0x5A: e_=d_; break; case 0x5B: break;
            case 0x5C: e_=h_; break; case 0x5D: e_=l_; break; case 0x5E: e_=readByte((h_<<8)|l_); break; case 0x5F: e_=a_; break;
            case 0x60: h_=b_; break; case 0x61: h_=c_; break; case 0x62: h_=d_; break; case 0x63: h_=e_; break;
            case 0x64: break; case 0x65: h_=l_; break; case 0x66: h_=readByte((h_<<8)|l_); break; case 0x67: h_=a_; break;
            case 0x68: l_=b_; break; case 0x69: l_=c_; break; case 0x6A: l_=d_; break; case 0x6B: l_=e_; break;
            case 0x6C: l_=h_; break; case 0x6D: break; case 0x6E: l_=readByte((h_<<8)|l_); break; case 0x6F: l_=a_; break;
            case 0x70: writeByte((h_<<8)|l_,b_); break; case 0x71: writeByte((h_<<8)|l_,c_); break;
            case 0x72: writeByte((h_<<8)|l_,d_); break; case 0x73: writeByte((h_<<8)|l_,e_); break;
            case 0x74: writeByte((h_<<8)|l_,h_); break; case 0x75: writeByte((h_<<8)|l_,l_); break;
            case 0x76:
                if (ime_) halted_ = true;
                else if (ie_ & iflag_ & 0x1F) haltBug_ = true;
                else halted_ = true;
                break;
            case 0x77: writeByte((h_<<8)|l_,a_); break;
            case 0x78: a_=b_; break; case 0x79: a_=c_; break; case 0x7A: a_=d_; break; case 0x7B: a_=e_; break;
            case 0x7C: a_=h_; break; case 0x7D: a_=l_; break; case 0x7E: a_=readByte((h_<<8)|l_); break; case 0x7F: break;

            // ALU (0x80-0xBF)
            case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87: {
                uint8_t v; switch(op&7){case 0:v=b_;break;case 1:v=c_;break;case 2:v=d_;break;case 3:v=e_;break;case 4:v=h_;break;case 5:v=l_;break;case 6:v=readByte((h_<<8)|l_);break;default:v=a_;}
                uint16_t r=a_+v; f_=((r&0xFF)?0:0x80)|((r>0xFF)?0x10:0)|(((a_&0xF)+(v&0xF)>0xF)?0x20:0); a_=r;
            } break;
            case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
                uint8_t v; switch(op&7){case 0:v=b_;break;case 1:v=c_;break;case 2:v=d_;break;case 3:v=e_;break;case 4:v=h_;break;case 5:v=l_;break;case 6:v=readByte((h_<<8)|l_);break;default:v=a_;}
                uint8_t cy=(f_>>4)&1; uint16_t r=a_+v+cy; f_=((r&0xFF)?0:0x80)|((r>0xFF)?0x10:0)|(((a_&0xF)+(v&0xF)+cy>0xF)?0x20:0); a_=r;
            } break;
            case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
                uint8_t v; switch(op&7){case 0:v=b_;break;case 1:v=c_;break;case 2:v=d_;break;case 3:v=e_;break;case 4:v=h_;break;case 5:v=l_;break;case 6:v=readByte((h_<<8)|l_);break;default:v=a_;}
                uint8_t r=a_-v; f_=(r?0:0x80)|0x40|((a_<v)?0x10:0)|(((a_&0xF)<(v&0xF))?0x20:0); a_=r;
            } break;
            case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
                uint8_t v; switch(op&7){case 0:v=b_;break;case 1:v=c_;break;case 2:v=d_;break;case 3:v=e_;break;case 4:v=h_;break;case 5:v=l_;break;case 6:v=readByte((h_<<8)|l_);break;default:v=a_;}
                uint8_t cy=(f_>>4)&1; uint16_t sum=v+cy; f_=(((uint8_t)(a_-sum))?0:0x80)|0x40|((a_<sum)?0x10:0)|(((a_&0xF)<(v&0xF)+cy)?0x20:0); a_-=sum;
            } break;
            case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7: {
                uint8_t v; switch(op&7){case 0:v=b_;break;case 1:v=c_;break;case 2:v=d_;break;case 3:v=e_;break;case 4:v=h_;break;case 5:v=l_;break;case 6:v=readByte((h_<<8)|l_);break;default:v=a_;}
                a_&=v; f_=(a_?0:0x80)|0x20;
            } break;
            case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: {
                uint8_t v; switch(op&7){case 0:v=b_;break;case 1:v=c_;break;case 2:v=d_;break;case 3:v=e_;break;case 4:v=h_;break;case 5:v=l_;break;case 6:v=readByte((h_<<8)|l_);break;default:v=a_;}
                a_^=v; f_=a_?0:0x80;
            } break;
            case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: {
                uint8_t v; switch(op&7){case 0:v=b_;break;case 1:v=c_;break;case 2:v=d_;break;case 3:v=e_;break;case 4:v=h_;break;case 5:v=l_;break;case 6:v=readByte((h_<<8)|l_);break;default:v=a_;}
                a_|=v; f_=a_?0:0x80;
            } break;
            case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
                uint8_t v; switch(op&7){case 0:v=b_;break;case 1:v=c_;break;case 2:v=d_;break;case 3:v=e_;break;case 4:v=h_;break;case 5:v=l_;break;case 6:v=readByte((h_<<8)|l_);break;default:v=a_;}
                f_=((a_==v)?0x80:0)|0x40|((a_<v)?0x10:0)|(((a_&0xF)<(v&0xF))?0x20:0);
            } break;

            // Control flow (0xC0-0xFF)
            case 0xC0: if (!(f_&0x80)){pc_=readByte(sp_)|(readByte(sp_+1)<<8);sp_+=2;branchExtra=12;} break;
            case 0xC1: c_=readByte(sp_);b_=readByte(sp_+1);sp_+=2; break;
            case 0xC2: {uint16_t ad=fetch16();if(!(f_&0x80)){pc_=ad;branchExtra=4;}} break;
            case 0xC3: pc_=readByte(pc_)|(readByte(pc_+1)<<8); break;
            case 0xC4: {uint16_t ad=fetch16();if(!(f_&0x80)){sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=ad;branchExtra=12;}} break;
            case 0xC5: sp_-=2;writeByte(sp_,c_);writeByte(sp_+1,b_); break;
            case 0xC6: {uint8_t v=readByte(pc_++);uint16_t r=a_+v;f_=((r&0xFF)?0:0x80)|((r>0xFF)?0x10:0)|(((a_&0xF)+(v&0xF)>0xF)?0x20:0);a_=r;} break;
            case 0xC7: sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=0x00; break;
            case 0xC8: if(f_&0x80){pc_=readByte(sp_)|(readByte(sp_+1)<<8);sp_+=2;branchExtra=12;} break;
            case 0xC9: pc_=readByte(sp_)|(readByte(sp_+1)<<8);sp_+=2; break;
            case 0xCA: {uint16_t ad=fetch16();if(f_&0x80){pc_=ad;branchExtra=4;}} break;
            case 0xCB: { // CB prefix — return directly with CB cycle count
                uint8_t cb=readByte(pc_++);
                uint8_t idx=cb&0x07;
                uint8_t val; switch(idx){case 0:val=b_;break;case 1:val=c_;break;case 2:val=d_;break;case 3:val=e_;break;case 4:val=h_;break;case 5:val=l_;break;case 6:val=readByte((h_<<8)|l_);break;default:val=a_;}
                if(cb<0x40){
                    uint8_t result;
                    switch((cb>>3)&0x07){
                        case 0:{uint8_t c7=val>>7;result=(val<<1)|c7;f_=(result?0:0x80)|(c7?0x10:0);}break;
                        case 1:{uint8_t c0=val&1;result=(val>>1)|(c0<<7);f_=(result?0:0x80)|(c0?0x10:0);}break;
                        case 2:{uint8_t c7=val>>7;result=(val<<1)|((f_>>4)&1);f_=(result?0:0x80)|(c7?0x10:0);}break;
                        case 3:{uint8_t c0=val&1;result=(val>>1)|((f_&0x10)<<3);f_=(result?0:0x80)|(c0?0x10:0);}break;
                        case 4:{uint8_t c7=val>>7;result=val<<1;f_=(result?0:0x80)|(c7?0x10:0);}break;
                        case 5:{uint8_t c0=val&1;result=(val>>1)|(val&0x80);f_=(result?0:0x80)|(c0?0x10:0);}break;
                        case 6:result=((val&0x0F)<<4)|((val>>4)&0x0F);f_=result?0:0x80;break;
                        default:{uint8_t c0=val&1;result=val>>1;f_=(result?0:0x80)|(c0?0x10:0);}break;
                    }
                    switch(idx){case 0:b_=result;break;case 1:c_=result;break;case 2:d_=result;break;case 3:e_=result;break;case 4:h_=result;break;case 5:l_=result;break;case 6:writeByte((h_<<8)|l_,result);break;default:a_=result;}
                }else if(cb<0x80){
                    uint8_t bit=(cb>>3)&0x07;
                    f_=(f_&0x10)|0x20|((val&(1<<bit))?0:0x80);
                }else if(cb<0xC0){
                    uint8_t bit=(cb>>3)&0x07;val&=~(1<<bit);
                    switch(idx){case 0:b_=val;break;case 1:c_=val;break;case 2:d_=val;break;case 3:e_=val;break;case 4:h_=val;break;case 5:l_=val;break;case 6:writeByte((h_<<8)|l_,val);break;default:a_=val;}
                }else{
                    uint8_t bit=(cb>>3)&0x07;val|=(1<<bit);
                    switch(idx){case 0:b_=val;break;case 1:c_=val;break;case 2:d_=val;break;case 3:e_=val;break;case 4:h_=val;break;case 5:l_=val;break;case 6:writeByte((h_<<8)|l_,val);break;default:a_=val;}
                }
                return cbCycles_[cb];
            }
            case 0xCC: {uint16_t ad=fetch16();if(f_&0x80){sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=ad;branchExtra=12;}} break;
            case 0xCD: {uint16_t ad=fetch16();sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=ad;} break;
            case 0xCE: {uint8_t v=readByte(pc_++);uint8_t cy=(f_>>4)&1;uint16_t r=a_+v+cy;f_=((r&0xFF)?0:0x80)|((r>0xFF)?0x10:0)|(((a_&0xF)+(v&0xF)+cy>0xF)?0x20:0);a_=r;} break;
            case 0xCF: sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=0x08; break;
            case 0xD0: if(!(f_&0x10)){pc_=readByte(sp_)|(readByte(sp_+1)<<8);sp_+=2;branchExtra=12;} break;
            case 0xD1: e_=readByte(sp_);d_=readByte(sp_+1);sp_+=2; break;
            case 0xD2: {uint16_t ad=fetch16();if(!(f_&0x10)){pc_=ad;branchExtra=4;}} break;
            case 0xD4: {uint16_t ad=fetch16();if(!(f_&0x10)){sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=ad;branchExtra=12;}} break;
            case 0xD5: sp_-=2;writeByte(sp_,e_);writeByte(sp_+1,d_); break;
            case 0xD6: {uint8_t v=readByte(pc_++);f_=((a_==v)?0x80:0)|0x40|((a_<v)?0x10:0)|(((a_&0xF)<(v&0xF))?0x20:0);a_-=v;} break;
            case 0xD7: sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=0x10; break;
            case 0xD8: if(f_&0x10){pc_=readByte(sp_)|(readByte(sp_+1)<<8);sp_+=2;branchExtra=12;} break;
            case 0xD9: ime_=true;pc_=readByte(sp_)|(readByte(sp_+1)<<8);sp_+=2; break; // RETI: immediate IME enable
            case 0xDA: {uint16_t ad=fetch16();if(f_&0x10){pc_=ad;branchExtra=4;}} break;
            case 0xDC: {uint16_t ad=fetch16();if(f_&0x10){sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=ad;branchExtra=12;}} break;
            case 0xDE: {uint8_t v=readByte(pc_++);uint8_t cy=(f_>>4)&1;uint16_t sum=v+cy;f_=(((uint8_t)(a_-sum))?0:0x80)|0x40|((a_<sum)?0x10:0)|(((a_&0xF)<(v&0xF)+cy)?0x20:0);a_-=sum;} break;
            case 0xDF: sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=0x18; break;
            case 0xE0: writeByte(0xFF00+readByte(pc_++),a_); break;
            case 0xE1: l_=readByte(sp_);h_=readByte(sp_+1);sp_+=2; break;
            case 0xE2: writeByte(0xFF00+c_,a_); break;
            case 0xE5: sp_-=2;writeByte(sp_,l_);writeByte(sp_+1,h_); break;
            case 0xE6: {uint8_t v=readByte(pc_++);a_&=v;f_=(a_?0:0x80)|0x20;} break;
            case 0xE7: sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=0x20; break;
            case 0xE8: {int8_t n=(int8_t)readByte(pc_++);f_=(((sp_&0xFF)+(n&0xFF)>0xFF)?0x10:0)|(((sp_&0xF)+(n&0xF)>0xF)?0x20:0);sp_+=n;} break;
            case 0xE9: pc_=(h_<<8)|l_; break;
            case 0xEA: {uint16_t ad=fetch16();writeByte(ad,a_);} break;
            case 0xEE: a_^=readByte(pc_++);f_=a_?0:0x80; break;
            case 0xEF: sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=0x28; break;
            case 0xF0: a_=readByte(0xFF00+readByte(pc_++)); break;
            case 0xF1: f_=readByte(sp_)&0xF0;a_=readByte(sp_+1);sp_+=2; break;
            case 0xF2: a_=readByte(0xFF00+c_); break;
            case 0xF3: ime_=false; break;
            case 0xF5: sp_-=2;writeByte(sp_,f_);writeByte(sp_+1,a_); break;
            case 0xF6: a_|=readByte(pc_++);f_=a_?0:0x80; break;
            case 0xF7: sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=0x30; break;
            case 0xF8: {int8_t n=(int8_t)readByte(pc_++);f_=(((sp_&0xFF)+(n&0xFF)>0xFF)?0x10:0)|(((sp_&0xF)+(n&0xF)>0xF)?0x20:0);uint16_t r=sp_+n;l_=r&0xFF;h_=r>>8;} break;
            case 0xF9: sp_=(h_<<8)|l_; break;
            case 0xFA: a_=readByte(fetch16()); break;
            case 0xFB: eiPending_=true; break;
            case 0xFE: {uint8_t v=readByte(pc_++);f_=((a_==v)?0x80:0)|0x40|((a_<v)?0x10:0)|(((a_&0xF)<(v&0xF))?0x20:0);} break;
            case 0xFF: sp_-=2;writeByte(sp_,pc_&0xFF);writeByte(sp_+1,pc_>>8);pc_=0x38; break;
            default: break;
        }
        return opCycles_[op] + branchExtra;
    }

    // ====== PPU ======
    // applyPalette returns a shade 0-3 that's been remapped through the
    // user's chosen e-ink theme. The first lookup is the standard GB
    // palette register decode; the second is the SumiBoy theme remap that
    // shifts shade balance for night reading / high contrast / etc.
    inline uint8_t applyPalette(uint8_t palette, uint8_t colorIdx) {
        uint8_t shade = (palette >> (colorIdx * 2)) & 0x03;
        return shadeRemap_[shade];
    }

    void renderLine(int line) {
        if (!(lcdc_ & 0x80)) return;
        // Work in an unpacked 160-byte stack buffer so the per-pixel PPU
        // writes stay simple, then pack 2bpp into framebuf at the end.
        uint8_t row[GB_W];
        memset(row, 0, GB_W);
        uint8_t colorRow[GB_W];
        if (isCgb_) memset(colorRow, 0, GB_W);
        uint8_t bgPriority[GB_W];
        memset(bgPriority, 0, GB_W);

        // Background
        if ((lcdc_ & 0x01) || isCgb_) {
            uint16_t mapBase = (lcdc_ & 0x08) ? 0x1C00 : 0x1800;
            uint16_t dataBase = (lcdc_ & 0x10) ? 0x0000 : 0x0800;
            bool signedIdx = !(lcdc_ & 0x10);
            int y = (line + scy_) & 0xFF;
            int mapRow = mapBase + (y / 8) * 32;
            for (int x = 0; x < GB_W;) {
                int sx = (x + scx_) & 0xFF;
                int mapOffset = mapRow + sx / 8;
                uint8_t tileIdx = vram[mapOffset];
                uint8_t attr = isCgb_ ? vram1_[mapOffset] : 0;
                uint16_t tileAddr;
                if (signedIdx) tileAddr = dataBase + ((int8_t)tileIdx + 128) * 16;
                else tileAddr = dataBase + tileIdx * 16;
                int tileRow = ((attr & 0x40) ? 7 - (y & 7) : (y & 7)) * 2;
                const uint8_t *tileBank = (attr & 0x08) ? vram1_ : vram;
                uint8_t lo = tileBank[tileAddr + tileRow];
                uint8_t hi = tileBank[tileAddr + tileRow + 1];
                int end = x + 8 - (sx & 7);
                if (end > GB_W) end = GB_W;
                if (isCgb_) {
                    for (; x < end; x++) {
                        int bit = (attr & 0x20) ? ((x + scx_) & 7) : 7 - ((x + scx_) & 7);
                        uint8_t colorIdx = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
                        bgPriority[x] = colorIdx | (attr & 0x80 ? 4 : 0);
                        row[x] = applyPalette(bgp_, colorIdx);
                        colorRow[x] = (attr & 7) * 4 + colorIdx;
                    }
                } else {
                    for (int bit = 7 - (sx & 7); x < end; x++, bit--) {
                        uint8_t colorIdx = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
                        bgPriority[x] = colorIdx;
                        row[x] = applyPalette(bgp_, colorIdx);
                    }
                }
            }
        } else {
            memset(row, 0, GB_W);
        }

        // Window (uses internal line counter that only increments when window is drawn)
        if ((lcdc_ & 0x20) && ((lcdc_ & 0x01) || isCgb_) && line >= wy_ && wx_ <= 166) {
            uint16_t winMap = (lcdc_ & 0x40) ? 0x1C00 : 0x1800;
            uint16_t dataBase = (lcdc_ & 0x10) ? 0x0000 : 0x0800;
            bool signedIdx = !(lcdc_ & 0x10);
            int winY = windowLine_;
            int winStartX = wx_ - 7;
            int mapRow = winMap + (winY / 8) * 32;
            for (int x = (winStartX < 0 ? 0 : winStartX); x < GB_W;) {
                int winX = x - winStartX;
                int mapOffset = mapRow + winX / 8;
                uint8_t tileIdx = vram[mapOffset];
                uint8_t attr = isCgb_ ? vram1_[mapOffset] : 0;
                uint16_t tileAddr;
                if (signedIdx) tileAddr = dataBase + ((int8_t)tileIdx + 128) * 16;
                else tileAddr = dataBase + tileIdx * 16;
                int tileRow = ((attr & 0x40) ? 7 - (winY & 7) : (winY & 7)) * 2;
                const uint8_t *tileBank = (attr & 0x08) ? vram1_ : vram;
                uint8_t lo = tileBank[tileAddr + tileRow];
                uint8_t hi = tileBank[tileAddr + tileRow + 1];
                int end = x + 8 - (winX & 7);
                if (end > GB_W) end = GB_W;
                if (isCgb_) {
                    for (; x < end; x++) {
                        int bit = (attr & 0x20) ? ((x - winStartX) & 7) :
                                                     7 - ((x - winStartX) & 7);
                        uint8_t colorIdx = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
                        bgPriority[x] = colorIdx | (attr & 0x80 ? 4 : 0);
                        row[x] = applyPalette(bgp_, colorIdx);
                        colorRow[x] = (attr & 7) * 4 + colorIdx;
                    }
                } else {
                    for (int bit = 7 - (winX & 7); x < end; x++, bit--) {
                        uint8_t colorIdx = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
                        bgPriority[x] = colorIdx;
                        row[x] = applyPalette(bgp_, colorIdx);
                    }
                }
            }
            windowLine_++;  // Only increment when window was actually rendered
        }

        // Sprites — DMG priority rules:
        //   1. Scan OAM 0→39, collect first 10 sprites whose Y range includes
        //      the current scanline (hardware 10-per-line limit).
        //   2. For those 10, sprites with LOWER X coordinate have HIGHER
        //      visual priority (appear on top). Ties broken by OAM index.
        //   3. Render them in reverse priority order so the highest-priority
        //      sprite is drawn LAST (which means it overwrites lower ones).
        //
        // Before: we iterated OAM 0→39 and drew as we went. The later-OAM
        // sprite always won, which is correct for CGB mode but WRONG on DMG.
        // Games like Pokemon Gold/Silver with overlapping party sprites
        // and Link's Awakening with item popups showed the wrong sprite on
        // top in rare cases. For CGB games we still use OAM-index priority
        // (which matches by leaving unlimitedSprites_ flag alone).
        if (lcdc_ & 0x02) {
            int spriteHeight = (lcdc_ & 0x04) ? 16 : 8;
            // Indices into OAM for up to 10 sprites that match this scanline
            uint8_t visIdx[10];
            int visCount = 0;
            for (int i = 0; i < 40 && visCount < 10; i++) {
                int sy = oam[i*4] - 16;
                if (line < sy || line >= sy + spriteHeight) continue;
                visIdx[visCount++] = (uint8_t)i;
            }

            // Sort by X ascending (lower X = higher priority), ties by OAM index.
            // Insertion sort is fine for max 10 elements. CGB keeps OAM order
            // regardless of X so we skip the sort in CGB mode.
            if (!isCgb_) {
                for (int i = 1; i < visCount; i++) {
                    uint8_t cur = visIdx[i];
                    int curX = (int)oam[cur*4 + 1];
                    int j = i;
                    while (j > 0) {
                        int prevX = (int)oam[visIdx[j-1]*4 + 1];
                        if (prevX < curX || (prevX == curX && visIdx[j-1] < cur)) break;
                        visIdx[j] = visIdx[j-1];
                        j--;
                    }
                    visIdx[j] = cur;
                }
            }

            // Render in reverse order: highest-priority (lowest X / lowest OAM
            // index after sort) drawn LAST so it overwrites lower-priority pixels.
            for (int k = visCount - 1; k >= 0; k--) {
                int i = visIdx[k];
                int sy = oam[i*4] - 16;
                int sx = oam[i*4+1] - 8;
                uint8_t tile = oam[i*4+2];
                uint8_t flags = oam[i*4+3];
                bool flipX = flags & 0x20;
                bool flipY = flags & 0x40;
                bool bgOver = flags & 0x80;
                uint8_t pal = (flags & 0x10) ? obp1_ : obp0_;
                int tileY = line - sy;
                if (flipY) tileY = spriteHeight - 1 - tileY;
                uint8_t useTile = tile;
                if (spriteHeight == 16) { useTile = (tile & 0xFE) + (tileY >= 8 ? 1 : 0); tileY &= 7; }
                uint16_t tileAddr = useTile * 16 + tileY * 2;
                const uint8_t *tileBank = (isCgb_ && (flags & 0x08)) ? vram1_ : vram;
                uint8_t lo = tileBank[tileAddr];
                uint8_t hi = tileBank[tileAddr + 1];
                for (int px = 0; px < 8; px++) {
                    int screenX = sx + px;
                    if (screenX < 0 || screenX >= GB_W) continue;
                    uint8_t bit = flipX ? px : (7 - px);
                    uint8_t colorIdx = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
                    if (colorIdx == 0) continue;
                    if (isCgb_) {
                        if ((lcdc_ & 1) && (bgPriority[screenX] & 3) &&
                            (bgOver || (bgPriority[screenX] & 4))) continue;
                    } else if (bgOver && bgPriority[screenX] != 0) continue;
                    row[screenX] = applyPalette(pal, colorIdx);
                    if (isCgb_) colorRow[screenX] = 32 + (flags & 7) * 4 + colorIdx;
                }
            }
        }

        // Pack 4 consecutive 2-bit shades per output byte.
        // byte layout: [px3<<6 | px2<<4 | px1<<2 | px0] — LSB is leftmost pixel.
        if (colorFramebuf_)
            memcpy(colorFramebuf_ + line * GB_W, colorRow, GB_W);
        if (framebuf) {
            uint8_t* packed = &framebuf[line * GB_FB_STRIDE];
            for (int x = 0; x < GB_W; x += 4) {
                packed[x >> 2] = (uint8_t)((row[x] & 0x3)
                                        | ((row[x + 1] & 0x3) << 2)
                                        | ((row[x + 2] & 0x3) << 4)
                                        | ((row[x + 3] & 0x3) << 6));
            }
        }
    }

    void runScanline() {
        // CGB double-speed mode: the CPU runs at 2x but the PPU and timer
        // stay at normal speed. That means each CPU T-cycle corresponds to
        // HALF a PPU/timer tick. We fake that by running twice as many
        // cpu_step()s per scanline — so the ~456 "scanline T-cycles" now
        // represents 456 PPU ticks (not CPU), which still wraps one line.
        //
        // Implementation note: we accumulate cpu_step() results as-is for
        // scanline budget (no per-step halving), and also feed them as-is
        // to updateTimer. The timer's frequency table stays the same, which
        // means in double-speed mode TIMA increments twice as often
        // (correct behavior per DMG docs: TIMA is CPU-clock-based).
        // The PPU effectively ticks at half the CPU rate because we've
        // doubled the budget to 912 T-cycles per line in double-speed.
        int budget = doubleSpeed_ ? 912 : 456;
        int cycles = 0;
        while (cycles < budget) {
            int c = cpu_step();
            updateTimer(c);
            cycles += c;
        }
    }

    // Helper: fire STAT interrupt only on rising edge of IRQ line
    inline void checkStatInterrupt() {
        bool line = false;
        if ((lcdstat_ & 0x40) && ly_ == lyc_) line = true;         // LYC match
        if ((lcdstat_ & 0x20) && ppuMode_ == 2) line = true;       // Mode 2 (OAM)
        if ((lcdstat_ & 0x10) && ppuMode_ == 1) line = true;       // Mode 1 (VBlank)
        if ((lcdstat_ & 0x08) && ppuMode_ == 0) line = true;       // Mode 0 (HBlank)
        if (line && !prevStatLine_) iflag_ |= 0x02;  // Rising edge only
        prevStatLine_ = line;
    }

    void runFrame() {
        windowLine_ = 0;  // Reset window internal line counter each frame
        int cycles = 0;
        for (ly_ = 0; ly_ < 154; ly_++) {
            const int speed = doubleSpeed_ ? 2 : 1;
            const int lineEnd = 456 * speed;
            auto runTo = [&](int end) {
                while (cycles < end) {
                    int c = cpu_step(end - cycles);
                    updateTimer(c);
                    cycles += c;
                }
            };
            if (ly_ < 144) {
                // Mode 2: OAM search (~20 cycles = 80 T-cycles)
                ppuMode_ = 2;
                checkStatInterrupt();
                runTo(80 * speed);

                // Render the line (mode 3: pixel transfer)
                ppuMode_ = 3;
                checkStatInterrupt();
                runTo(252 * speed);
                if (renderThisFrame_) renderLine(ly_);

                // Mode 0: HBlank. Polling games must execute in this phase.
                ppuMode_ = 0;
                checkStatInterrupt();
                runTo(lineEnd);
            } else {
                // VBlank (lines 144-153)
                if (ly_ == 144) {
                    ppuMode_ = 1;
                    iflag_ |= 0x01;  // VBlank interrupt
                    checkStatInterrupt();
                }
                runTo(lineEnd);
            }
            // Carry instruction overshoot into the next line, rather than
            // adding that time to every phase and slowing emulated timers.
            cycles -= lineEnd;
        }
    }
};

// SM83 base opcode T-cycles (conditional branches: NOT-TAKEN cost; branchExtra added for taken)
inline const uint8_t GBEmulator::opCycles_[256] = {
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
    4, 12,  8,  8,  4,  4,  8,  4, 20,  8,  8,  8,  4,  4,  8,  4, // 0x
    4, 12,  8,  8,  4,  4,  8,  4, 12,  8,  8,  8,  4,  4,  8,  4, // 1x
    8, 12,  8,  8,  4,  4,  8,  4,  8,  8,  8,  8,  4,  4,  8,  4, // 2x
    8, 12,  8,  8, 12, 12, 12,  4,  8,  8,  8,  8,  4,  4,  8,  4, // 3x
    4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 4x
    4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 5x
    4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 6x
    8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4, // 7x
    4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 8x
    4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 9x
    4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // Ax
    4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // Bx
    8, 12, 12, 16, 12, 16,  8, 16,  8, 16, 12,  4, 12, 24,  8, 16, // Cx
    8, 12, 12,  0, 12, 16,  8, 16,  8, 16, 12,  0, 12,  0,  8, 16, // Dx
   12, 12,  8,  0,  0, 16,  8, 16, 16,  4, 16,  0,  0,  0,  8, 16, // Ex
   12, 12,  8,  4,  0, 16,  8, 16, 12,  8, 16,  4,  0,  0,  8, 16, // Fx
};

// CB-prefixed opcode T-cycles (reg=8, (HL)=16 except BIT (HL)=12)
inline const uint8_t GBEmulator::cbCycles_[256] = {
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 0x RLC/RRC
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 1x RL/RR
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 2x SLA/SRA
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 3x SWAP/SRL
    8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8, // 4x BIT 0-1
    8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8, // 5x BIT 2-3
    8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8, // 6x BIT 4-5
    8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8, // 7x BIT 6-7
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 8x RES 0-1
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 9x RES 2-3
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // Ax RES 4-5
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // Bx RES 6-7
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // Cx SET 0-1
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // Dx SET 2-3
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // Ex SET 4-5
    8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // Fx SET 6-7
};
