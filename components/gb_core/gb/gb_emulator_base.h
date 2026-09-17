#pragma once
/**
 * Emulator Interface — abstract base for all emulated systems.
 * Each system (GB, CHIP-8, NES, ZX Spectrum) implements this interface.
 * main.cpp uses it via Emulator* pointer for system-agnostic game loop.
 */

#include <Arduino.h>
#include <SdFat.h>

// Generic button bitmask — each system maps these to its own input format
#define INPUT_RIGHT   0x01
#define INPUT_LEFT    0x02
#define INPUT_UP      0x04
#define INPUT_DOWN    0x08
#define INPUT_A       0x10   // Confirm / primary action
#define INPUT_B       0x20   // Back / secondary action
#define INPUT_START   0x40
#define INPUT_SELECT  0x80

// System type enum for ROM picker
enum SystemType {
    SYS_NONE = 0,
    SYS_GAMEBOY,
    SYS_CHIP8,
    SYS_NES,
    SYS_ZX_SPECTRUM,
    SYS_ZMACHINE,
    // SYS_DOOM is special — it's not a Emulator subclass. The "ROM" is the
    // shipped DOOM WAD baked into the OTA_1 app partition. Selecting it from
    // the carousel triggers an OTA partition switch + reboot into the DOOM
    // firmware. See launchDoom() in main.cpp.
    SYS_DOOM
};

// Safe-write helper: write to .tmp file, then atomic 3-rename rotation
// (matches docs/ATOMIC_WRITE_DESIGN.md so a power-cut at any point in
// the rotation leaves at least one valid file copy on disk).
//
// Pre-audit pass this used a simpler `remove(canonical) +
// rename(tmp, canonical)` pair, which has a window between remove and
// rename where NO save file exists at all. A power loss in that
// window = lost saved state. The rotation below extends the window
// to: at every moment either canonical exists (the old or new
// version), or both .tmp and .bak exist. recoverAtomicWrites in
// SDCardManager scans /.sumi (depth 2), /notes (depth 0),
// /data (depth 0), /custom (depth 1), and /games (depth 1) — orphan
// .tmp/.bak left here by an interrupted save are cleaned up on next
// boot the same way the canonical /.sumi/* writers are.
inline void buildTmpPath(const char* path, char* tmpPath, int tmpPathSize) {
    snprintf(tmpPath, tmpPathSize, "%s.tmp", path);
}

inline bool safeRename(SdFat& sd, const char* tmpPath, const char* finalPath) {
    char bakPath[96];
    snprintf(bakPath, sizeof(bakPath), "%s.bak", finalPath);

    // Step 1: clear any leftover .bak from a previous interrupted save.
    if (sd.exists(bakPath)) sd.remove(bakPath);

    // Step 2: rotate canonical → .bak (only if canonical exists).
    bool hadCanonical = sd.exists(finalPath);
    if (hadCanonical) {
        if (!sd.rename(finalPath, bakPath)) {
            sd.remove(tmpPath);
            return false;
        }
    }

    // Step 3: promote .tmp → canonical.
    if (!sd.rename(tmpPath, finalPath)) {
        // Roll back: try to restore .bak as canonical.
        if (hadCanonical) sd.rename(bakPath, finalPath);
        sd.remove(tmpPath);
        return false;
    }

    // Step 4: best-effort cleanup of .bak.
    if (hadCanonical) sd.remove(bakPath);
    return true;
}

class Emulator {
public:
    virtual ~Emulator() {}

    // Initialize emulator state (called once after construction)
    virtual bool init() = 0;

    // Load a ROM. romData is a directly addressable read-only ROM (may be nullptr).
    // path is the SD card path for fallback reads.
    // Returns true on success.
    virtual bool loadRom(const char* path, const uint8_t* romData, uint32_t romSize) = 0;

    // Run N frames of emulation
    virtual void runFrames(int count) = 0;

    // Set input state (bitmask of INPUT_* flags, 1 = pressed)
    virtual void setInput(uint8_t buttons) = 0;

    // Get framebuffer — array of shade values (0=white, 3=black)
    virtual uint8_t* getFramebuffer() = 0;

    // Native resolution of this system
    virtual int getWidth() = 0;
    virtual int getHeight() = 0;

    // Save/load full emulator state
    virtual bool saveState(SdFat& sd, const char* path) = 0;
    virtual bool loadState(SdFat& sd, const char* path) = 0;

    // Save/load battery-backed SRAM (lightweight, just game saves)
    virtual bool saveSRAM(SdFat& sd, const char* path) { return true; }
    virtual bool loadSRAM(SdFat& sd, const char* path) { return true; }

    // Reset emulator to power-on state
    virtual void reset() = 0;

    // How many warmup frames to run on first boot (before display)
    virtual int getWarmupFrames() { return 0; }

    // Frame counter (for save state reference)
    int frameCount = 0;
};
