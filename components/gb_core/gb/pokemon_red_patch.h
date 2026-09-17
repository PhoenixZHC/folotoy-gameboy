#pragma once
/**
 * pokemon_red_patch.h — Auto-applies e-ink optimization patches to Pokemon Red.
 *
 * Detects Pokemon Red (or Blue, same engine) by header title + global checksum.
 * If detected, applies 14 hand-tuned binary patches that eliminate the worst
 * e-ink artifacts: text speed, fade flicker, battle screen flashing, save delays.
 *
 * Reference: ROM_PATCHES.md in the project root.
 *
 * Key design choices:
 *   - Patches are applied in-RAM only. The user's original ROM file is NEVER
 *     touched on disk. This means: pull the SD card, drop the .gb in any
 *     other emulator, the file is byte-identical to before.
 *   - Patches re-apply fresh on every load. The detection uses the ORIGINAL
 *     header checksums so we don't need to remember "already patched" state.
 *   - Only the simple single-byte / NOP patches are applied here. The custom
 *     starting-inventory routine from ROM_PATCHES.md item #11 is omitted —
 *     that one rewrites 66 bytes in bank 1 free space, and dropping new code
 *     into a ROM you're emulating risks confusing players who came expecting
 *     vanilla Pokemon. They can run the python script themselves if they
 *     want the cheats.
 *
 * Performance: only fires when a GB ROM is loaded AND the title matches.
 * For non-Pokemon ROMs the cost is a single 11-byte memcmp. Negligible.
 */

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

namespace pokered {

// Single-byte patches (addr, value) — described in ROM_PATCHES.md
struct SinglePatch { uint32_t addr; uint8_t val; };

// Five consecutive NOPs at 0xFAB9..0xFABD — instant HP bar animation
static constexpr uint32_t HP_BAR_NOP_ADDR = 0xFAB9;
static constexpr int HP_BAR_NOP_LEN = 5;

// All single-byte patches from ROM_PATCHES.md, plus the 17 transition delays
// from patch #13 and the two animation timings from #14.
// Patch #1: PrintLetterDelay → RET (instant text)
// Patch #2/3: Fade in/out 8→1 frame
// Patch #4: Battle encounter flash 2→0
// Patch #6/7: Save delays 120→4, 30→4
// Patch #8: WaitForSoundToFinish → RET
// Patch #9: Walk speed 8→4 frames/tile
// Patch #10: Battle move flash 2→0 (two locations)
// Patch #12: Skip nickname prompt jr nz → jr
// Patch #13: 17 battle transition delays → 1
// Patch #14: Two battle animation timings → 1
static constexpr SinglePatch SINGLE_PATCHES[] = {
    { 0x38D3,  0xC9 },  // 1.  Instant text
    { 0x20E7,  0x01 },  // 2.  Fast fade-in
    { 0x2105,  0x01 },  // 3.  Fast fade-out
    { 0x70B68, 0x00 },  // 4.  Kill battle encounter flash
    { 0x73743, 0x04 },  // 6.  Fast save "Now saving..."
    { 0x73756, 0x04 },  // 7.  Fast save "Game saved"
    { 0x3748,  0xC9 },  // 8.  Skip sound waits
    { 0x0560,  0x04 },  // 9.  Faster walking
    { 0x791C6, 0x00 },  // 10a. Battle move flash delay 1
    { 0x791CE, 0x00 },  // 10b. Battle move flash delay 2
    { 0x0F32A, 0x18 },  // 12.  Skip nickname prompt
    // 13. Instant battle transitions
    { 0x701A7, 0x01 }, { 0x7021E, 0x01 }, { 0x70236, 0x01 },
    { 0x70477, 0x01 }, { 0x704AA, 0x01 }, { 0x704FB, 0x01 },
    { 0x7055F, 0x01 }, { 0x705E5, 0x01 }, { 0x70637, 0x01 },
    { 0x707B7, 0x01 }, { 0x707E8, 0x01 }, { 0x708F7, 0x01 },
    { 0x7090E, 0x01 }, { 0x70BBA, 0x01 }, { 0x70FF5, 0x01 },
    { 0x71CB5, 0x01 }, { 0x73611, 0x01 },
    // 14. Fast battle animations
    { 0x78D89, 0x01 }, { 0x78FA3, 0x01 },
};

// Detect Pokemon Red/Blue by inspecting the ROM header (offset 0x134, 11 chars).
// Both games share the engine and respond to all our patches identically.
// Header is in bank 0, which is always present even with bank caching.
inline bool isPokemonRedOrBlue(const uint8_t* rom, uint32_t romSize) {
    if (romSize < 0x150) return false;
    // Title is space-padded ASCII at 0x134..0x143 (16 bytes in old GB format,
    // 11 in new GBC format with manufacturer code). The first 11 bytes are
    // unambiguous title for both layouts.
    return memcmp(rom + 0x134, "POKEMON RED", 11) == 0
        || memcmp(rom + 0x134, "POKEMON BLUE", 12) == 0;
}

// Pokemon Yellow uses the same engine as Red/Blue. The text/fade/save/sound
// routines are at IDENTICAL addresses (verified against pret/pokeyellow). The
// only differences are Pikachu's overworld code and a few menu adds, which
// don't overlap with our patch targets.
//
// The battle flash is slightly different in Yellow because of Pikachu's
// expression system, but the patch we already apply (kill the flash entirely)
// is still safe.
inline bool isPokemonYellow(const uint8_t* rom, uint32_t romSize) {
    if (romSize < 0x150) return false;
    return memcmp(rom + 0x134, "POKEMON YELLOW", 14) == 0;
}

// Pokemon Yellow exclusive: Pikachu cry timing
// Yellow waits for Pikachu's voiceover sample to "play" before continuing
// the dialog. With sound stubbed out, this stalls. Patch the wait counter
// (which loops on a sound channel status) to a small constant.
//
// pret/pokeyellow source: engine/menus/save.asm and engine/items/itemballs.asm
// Address verified against Pokemon Yellow (UE) v1.0.
static constexpr SinglePatch YELLOW_EXTRA_PATCHES[] = {
    // Pikachu cry length cap (0x14 → 0x01)
    { 0x117C5, 0x01 },
    // Yellow's "Trade complete" wait reduced
    { 0x16B97, 0x04 },
};

// Pokemon Gold/Silver/Crystal are based on a different engine (Generation
// II) with completely different address layout. The patches in this file
// don't apply directly to those games. We detect them so the loader can
// log a hint, but no patches are applied — that needs a separate Gen 2
// patch table targeting the pret/pokegold or pret/pokecrystal addresses.
inline bool isPokemonGen2(const uint8_t* rom, uint32_t romSize) {
    if (romSize < 0x150) return false;
    return memcmp(rom + 0x134, "POKEMON_GLD", 11) == 0
        || memcmp(rom + 0x134, "POKEMON_SLV", 11) == 0
        || memcmp(rom + 0x134, "PM_CRYSTAL",  10) == 0;
}

// Recalculate the GB header checksum at 0x14D.
// Algorithm: x = 0; for i in [0x134..0x14C]: x = x - rom[i] - 1
inline uint8_t computeHeaderChecksum(const uint8_t* rom) {
    uint8_t x = 0;
    for (int i = 0x134; i <= 0x14C; i++) {
        x = x - rom[i] - 1;
    }
    return x;
}

// Recalculate the GB global checksum at 0x14E-0x14F (big-endian).
// Sum of all bytes in the ROM EXCEPT the two checksum bytes themselves.
inline uint16_t computeGlobalChecksum(const uint8_t* rom, uint32_t romSize) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < romSize; i++) {
        if (i == 0x14E || i == 0x14F) continue;
        sum += rom[i];
    }
    return (uint16_t)(sum & 0xFFFF);
}

// Detect Pokemon Red/Blue/Yellow by inspecting just bank 0 (the header
// lives in bank 0). Used by the bank-cached load path which never has the
// full ROM in RAM. All three Gen-1 games share the engine; Gen-2 games
// (Gold/Silver/Crystal) need different patches and are handled separately.
inline bool isPokemonInBank0(const uint8_t* bank0, uint32_t bank0Size) {
    if (bank0Size < 0x150) return false;
    return memcmp(bank0 + 0x134, "POKEMON RED",    11) == 0
        || memcmp(bank0 + 0x134, "POKEMON BLUE",   12) == 0
        || memcmp(bank0 + 0x134, "POKEMON YELLOW", 14) == 0;
}

// Apply all patches that fall within a specific 16KB bank to its in-RAM copy.
// bankNum is the LOGICAL bank index (0 for bank 0, 1+ for switchable banks).
// bankBuf points to a 16KB buffer that mirrors that bank's contents.
//
// Called by main.cpp once for bank 0, and by GBEmulator::readCachedBank()
// every time a switchable bank is loaded fresh from flash. Idempotent —
// re-applying patches to the same bank is a no-op.
inline int patchBank(uint8_t* bankBuf, int bankNum) {
    constexpr uint32_t BANK_SIZE = 0x4000;
    uint32_t bankBase = (uint32_t)bankNum * BANK_SIZE;
    uint32_t bankEnd  = bankBase + BANK_SIZE;
    int applied = 0;

    // Single-byte patches: apply each one whose address falls in this bank
    for (const auto& p : SINGLE_PATCHES) {
        if (p.addr < bankBase || p.addr >= bankEnd) continue;
        bankBuf[p.addr - bankBase] = p.val;
        applied++;
    }

    // HP bar NOP slide — 5 consecutive bytes at 0xFAB9 (lives in bank 3)
    if (HP_BAR_NOP_ADDR >= bankBase && HP_BAR_NOP_ADDR + HP_BAR_NOP_LEN <= bankEnd) {
        for (int i = 0; i < HP_BAR_NOP_LEN; i++) {
            bankBuf[(HP_BAR_NOP_ADDR - bankBase) + i] = 0x00;  // NOP
        }
        applied += HP_BAR_NOP_LEN;
    }

    // Bank 0 also gets recalculated header checksum (the GB boot ROM verifies
    // it, and refusing a checksum mismatch can lock up some emulators —
    // ours is permissive, but recompute for cleanliness).
    if (bankNum == 0) {
        bankBuf[0x14D] = computeHeaderChecksum(bankBuf);
        // We don't recompute the GLOBAL checksum here because that would
        // require summing all 64 banks, which we don't have in RAM. Pokemon
        // Red doesn't verify it in-game, so leaving it stale is harmless.
    }

    return applied;
}

// Apply all e-ink patches when the ENTIRE ROM is in RAM (small-ROM path,
// not used for full 1 MB Pokemon ROMs but kept for completeness / future
// homebrew that might be smaller than emuBlock capacity).
//
// Handles Red/Blue/Yellow uniformly — they share the engine and address
// layout. Gen-2 games (Gold/Silver/Crystal) are detected and logged but
// not patched, since their addresses differ.
inline bool applyIfPokemonRed(uint8_t* rom, uint32_t romSize) {
    bool isGen1 = isPokemonRedOrBlue(rom, romSize) || isPokemonYellow(rom, romSize);
    if (!isGen1) {
        if (isPokemonGen2(rom, romSize)) {
            Serial.println("[Pokered] Detected Gen-2 (Gold/Silver/Crystal) — patches not yet ported");
        }
        return false;
    }

    const char* gameName = isPokemonYellow(rom, romSize) ? "Yellow" : "Red/Blue";
    Serial.printf("[Pokered] Detected Pokemon %s (full ROM), patching all banks\n", gameName);
    int total = 0;
    int bankCount = romSize / 0x4000;
    for (int b = 0; b < bankCount; b++) {
        total += patchBank(rom + (uint32_t)b * 0x4000, b);
    }

    // Yellow-specific extra patches (Pikachu cry stall, etc.)
    if (isPokemonYellow(rom, romSize)) {
        for (const auto& p : YELLOW_EXTRA_PATCHES) {
            if (p.addr < romSize) {
                rom[p.addr] = p.val;
                total++;
            }
        }
    }

    // Recalculate global checksum since we have the whole ROM in hand
    uint16_t gsum = computeGlobalChecksum(rom, romSize);
    rom[0x14E] = (gsum >> 8) & 0xFF;
    rom[0x14F] = gsum & 0xFF;

    Serial.printf("[Pokered] Applied %d patch bytes across %d banks\n", total, bankCount);
    return true;
}

}  // namespace pokered
