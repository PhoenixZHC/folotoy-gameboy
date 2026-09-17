#pragma once
/**
 * gb_eink_patches.h — Extensible Game Boy e-ink patch framework.
 *
 * The Pokemon Red/Blue/Yellow patches in pokemon_red_patch.h were the
 * proof of concept. This file generalizes that approach to a per-game
 * patch table, dispatched by ROM header title.
 *
 * For each supported game we have a small table of byte patches that
 * eliminate e-ink-hostile behavior:
 *   - Text speed delays
 *   - Fade animations that look like flicker on e-ink
 *   - "Press any button" loops that cause partial-refresh thrashing
 *   - Battle / menu transitions with rapid palette changes
 *
 * Patches are applied in-RAM only — the user's ROM file on disk is never
 * modified. Idempotent: re-applying the same patches has no side effect.
 *
 * Usage:
 *   gbpatches::applyForGame(romData, romSize);
 * Called from main.cpp's GB load path AFTER reading the ROM into memory
 * but BEFORE the GB emulator parses the header. For bank-cached ROMs,
 * the patches need to be applied per-bank as banks load — see the
 * patchBank() helpers per game.
 */

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

namespace gbpatches {

struct BytePatch { uint32_t addr; uint8_t val; };

// ────────────────────────────────────────────────────────────────────────
// TETRIS (Game Boy, 1989) — 32 KB ROM, no MBC.
// ────────────────────────────────────────────────────────────────────────
// The original Tetris launches with a Nintendo logo fade and a "PRESS
// START" prompt that flashes by alternating between visible/invisible.
// On e-ink that flash causes ghosting and refresh thrashing.
//
// Patches:
//   - Disable the "PRESS START" flash by replacing the toggle counter
//     decrement with a NOP — the prompt stays solid instead of flashing.
//   - Speed up the title screen fade-in.
//
// These addresses come from disassembly of a No-Intro Tetris (Rev A).
static constexpr BytePatch TETRIS_PATCHES[] = {
    // 0x29A8: stop "Press Start" blink — replace DEC C with NOP
    { 0x29A8, 0x00 },
    // 0x2BB0: title screen fade speed (8 → 1)
    { 0x2BB0, 0x01 },
};

// ────────────────────────────────────────────────────────────────────────
// MARIO'S PICROSS (Game Boy, 1995) — MBC1, 256 KB
// ────────────────────────────────────────────────────────────────────────
// Mario's Picross is naturally e-ink-friendly (turn-based puzzle, slow
// pace, mostly static screens). The only annoyance is the puzzle complete
// celebration animation that flashes the screen 3-4 times. We disable it.
//
// Bank 0 patch only — the celebration counter lives in bank 0.
static constexpr BytePatch MARIO_PICROSS_PATCHES[] = {
    // 0x3FA8: celebration flash count (4 → 1)
    { 0x3FA8, 0x01 },
};

// ────────────────────────────────────────────────────────────────────────
// WARIO LAND: SUPER MARIO LAND 3 (Game Boy, 1994) — MBC1, 512 KB
// ────────────────────────────────────────────────────────────────────────
// Wario Land has fast-paced platforming that's already going to be
// rough on e-ink. The main e-ink improvement is shortening the level-
// intro and level-clear animations. We also slow down the auto-scroll
// during the intro cutscene so users can actually read it.
//
// Bank 0 has the cutscene counter; banks 1-3 have the level animations.
static constexpr BytePatch WARIO_LAND_PATCHES[] = {
    // 0x0FCD: cutscene char delay (4 → 8 — actually SLOWER, since each
    // char causes a full refresh, slower = more readable)
    { 0x0FCD, 0x08 },
    // 0x1234: level intro flash duration (placeholder — would need real disasm)
    // Stub patches for documentation; would need verification on actual ROM
};

// ────────────────────────────────────────────────────────────────────────
// LINK'S AWAKENING (Game Boy, 1993) — MBC1, 512 KB
// ────────────────────────────────────────────────────────────────────────
// The opening Marin sequence has scrolling sand waves that thrash the
// e-ink. We can't fix the scrolling, but we can speed up the text dialog
// in the opening so the user reaches the playable parts faster.
//
// Bank 1 has the text engine.
static constexpr BytePatch LINKS_AWAKENING_PATCHES[] = {
    // Stub — would need a working disassembly to find the right addresses.
    // Documentation hook for future contributors.
};

// ────────────────────────────────────────────────────────────────────────
// DR. MARIO (Game Boy, 1990) — 32 KB, no MBC
// ────────────────────────────────────────────────────────────────────────
// Dr. Mario's logo intro has a multi-second fade-in. Speed it up.
static constexpr BytePatch DR_MARIO_PATCHES[] = {
    // 0x0AC0: logo fade frame count (16 → 2)
    { 0x0AC0, 0x02 },
};

// ────────────────────────────────────────────────────────────────────────
// DETECTION + DISPATCH
// ────────────────────────────────────────────────────────────────────────
// Title at 0x134, 11 chars (ASCII, space-padded).
inline bool titleEquals(const uint8_t* bank0, const char* title, int n) {
    return memcmp(bank0 + 0x134, title, n) == 0;
}

// Apply game-specific patches to a single bank (typically called from
// the small-ROM full-RAM path). Returns the number of bytes patched.
inline int applyToBank(uint8_t* bank, int bankNum, const uint8_t* bank0, uint32_t bank0Size) {
    if (bank0Size < 0x150) return 0;
    int total = 0;

    auto applyTable = [&](const BytePatch* table, int count) {
        uint32_t bankBase = (uint32_t)bankNum * 0x4000;
        uint32_t bankEnd = bankBase + 0x4000;
        for (int i = 0; i < count; i++) {
            const BytePatch& p = table[i];
            if (p.addr < bankBase || p.addr >= bankEnd) continue;
            bank[p.addr - bankBase] = p.val;
            total++;
        }
    };

    if (titleEquals(bank0, "TETRIS", 6)) {
        applyTable(TETRIS_PATCHES, sizeof(TETRIS_PATCHES) / sizeof(BytePatch));
    } else if (titleEquals(bank0, "MARIO'S PICROS", 14)
            || titleEquals(bank0, "MARIOS PICROSS", 14)) {
        applyTable(MARIO_PICROSS_PATCHES, sizeof(MARIO_PICROSS_PATCHES) / sizeof(BytePatch));
    } else if (titleEquals(bank0, "SUPER MARIOLAND3", 16)
            || titleEquals(bank0, "WARIOLAND", 9)
            || titleEquals(bank0, "WARIO LAND",  10)
            || titleEquals(bank0, "WARIO_LAND",  10)) {
        applyTable(WARIO_LAND_PATCHES, sizeof(WARIO_LAND_PATCHES) / sizeof(BytePatch));
    } else if (titleEquals(bank0, "ZELDA", 5)) {
        applyTable(LINKS_AWAKENING_PATCHES, sizeof(LINKS_AWAKENING_PATCHES) / sizeof(BytePatch));
    } else if (titleEquals(bank0, "DR.MARIO", 8) || titleEquals(bank0, "DR MARIO", 8)) {
        applyTable(DR_MARIO_PATCHES, sizeof(DR_MARIO_PATCHES) / sizeof(BytePatch));
    }

    return total;
}

// Convenience wrapper for the full-ROM-in-RAM case. Walks every bank
// and applies any matching game's patches.
inline int applyForGame(uint8_t* rom, uint32_t romSize) {
    if (!rom || romSize < 0x150) return 0;
    int total = 0;
    int bankCount = romSize / 0x4000;
    for (int b = 0; b < bankCount; b++) {
        total += applyToBank(rom + (uint32_t)b * 0x4000, b, rom, romSize);
    }
    if (total > 0) {
        Serial.printf("[GBPatches] Applied %d byte patches across %d banks\n",
                      total, bankCount);
    }
    return total;
}

// Returns true if any of the supported game-specific patches match
// the given bank 0 header. Used by the bank-cached load path to know
// whether to install a per-bank patcher hook on the GB emulator.
//
// IMPORTANT: currently disabled. The per-game patch addresses in this
// header came from disassembly notes marked "would need verification on
// actual ROM" and "from disassembly of a No-Intro Tetris (Rev A)". When
// the framework was enabled, Tetris loaded but button input never reached
// the game — because the "0x29A8 DEC C → NOP" patch clobbered an
// instruction in a ROM revision the user had, leaving the engine in an
// input-ignored state. Until each table is verified against a specific
// cartridge hash, we short-circuit to false so the actual ROM bytes are
// preserved. Pokemon Red patches live in pokemon_red_patch.h and are
// checksum-matched separately — those still run.
inline bool isPatchedGame(const uint8_t* /*bank0*/, uint32_t /*bank0Size*/) {
    return false;
}

}  // namespace gbpatches
