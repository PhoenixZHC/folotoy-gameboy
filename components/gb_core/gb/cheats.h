#pragma once
/**
 * cheats.h — Game Genie cheat code system for NES + Game Boy.
 *
 * Pre-shipped famous codes for popular games are auto-applied at load
 * time when the matching ROM is detected (by header). For v1.1 there's
 * no UI for entering custom codes — that's planned for a future release.
 *
 * Architecture:
 *   - A small fixed table of active cheats (max 16) holds the runtime
 *     cheat list. Each entry has a system tag, address, value, optional
 *     compare byte, and active flag.
 *   - cheats::lookup(sys, addr) is called from the relevant CPU read
 *     paths (NES mapperCpuRead, GB readByte). On hit it returns the
 *     overridden value; on miss it returns 0xFFFF as a sentinel.
 *   - Game-specific code packs are applied via cheats::applyForRom()
 *     called once at ROM load time.
 *
 * Game Genie format reference:
 *   NES: https://tuxnes.sourceforge.net/gamegenie.html
 *   GB:  https://gbhh.avivace.com/game-genie
 *
 * NES character → nibble: APZLGITYEOXUKSVN → 0-F
 * GB  character → nibble: 0-9 plus A-F (standard hex, but 6/9 chars only)
 */

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include "gb_emulator_base.h"

namespace cheats {

constexpr int MAX_ACTIVE = 16;
constexpr uint16_t MISS = 0xFFFF;

struct Cheat {
    SystemType system;
    uint16_t address;
    uint8_t value;
    uint8_t compare;        // 0xFF when unused (no compare check)
    bool useCompare;
    bool active;
    char label[24];          // Short description for the (future) UI
    char code[12];           // Original Game Genie code string (for re-save)
};

inline Cheat g_active[MAX_ACTIVE] = {};
inline int g_activeCount = 0;

// Clear all active cheats. Called by main.cpp when a ROM unloads.
inline void clearAll() {
    for (int i = 0; i < MAX_ACTIVE; i++) g_active[i].active = false;
    g_activeCount = 0;
}

// Add a cheat to the active list. Returns false if the table is full.
inline bool add(SystemType sys, uint16_t addr, uint8_t val, const char* label) {
    if (g_activeCount >= MAX_ACTIVE) return false;
    Cheat& c = g_active[g_activeCount++];
    c.system = sys;
    c.address = addr;
    c.value = val;
    c.compare = 0xFF;
    c.useCompare = false;
    c.active = true;
    strncpy(c.label, label ? label : "", sizeof(c.label) - 1);
    c.label[sizeof(c.label) - 1] = '\0';
    // Clear the code field — addNESCode/addGBCode fills it in after this
    // helper returns, but direct callers (applyNESPack, applyGBPack) never
    // supply a code string. Without clearing, a stale value from a prior
    // ROM's cheat list can leak into the save-to-disk representation and
    // make the next saveCheats() write garbage characters.
    c.code[0] = '\0';
    return true;
}

// Add a cheat with a compare byte. The cheat only fires if the original
// memory at `addr` equals `compare`. This is the 8-character NES Game Genie
// format and the standard for codes that target banked ROM regions.
inline bool addCompare(SystemType sys, uint16_t addr, uint8_t val, uint8_t cmp,
                       const char* label) {
    if (!add(sys, addr, val, label)) return false;
    Cheat& c = g_active[g_activeCount - 1];
    c.compare = cmp;
    c.useCompare = true;
    return true;
}

// Hot-path lookup. Called from emulator CPU read paths.
// `originalValue` is what the cartridge would return without the cheat —
// needed for the optional compare check. Returns the cheated value, or
// MISS (0xFFFF) if no cheat matches.
inline uint16_t lookup(SystemType sys, uint16_t addr, uint8_t originalValue) {
    if (g_activeCount == 0) return MISS;
    for (int i = 0; i < g_activeCount; i++) {
        const Cheat& c = g_active[i];
        if (!c.active) continue;
        if (c.system != sys) continue;
        if (c.address != addr) continue;
        if (c.useCompare && originalValue != c.compare) continue;
        return c.value;
    }
    return MISS;
}

// ────────────────────────────────────────────────────────────────────────
// Game Genie code decoder — NES (6 or 8 characters)
// ────────────────────────────────────────────────────────────────────────

inline int nesCharToNibble(char c) {
    static const char* table = "APZLGITYEOXUKSVN";
    for (int i = 0; i < 16; i++) if (table[i] == c) return i;
    // Lowercase fallback
    char up = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    for (int i = 0; i < 16; i++) if (table[i] == up) return i;
    return -1;
}

// Decode a Game Genie code into address + value (+ optional compare).
// Returns true on success. The encoding is wonky (bits get shuffled);
// this implementation is the standard reference.
inline bool decodeNES(const char* code, uint16_t* outAddr, uint8_t* outVal,
                     uint8_t* outCmp, bool* outHasCmp) {
    int n[8];
    int len = strlen(code);
    if (len != 6 && len != 8) return false;
    for (int i = 0; i < len; i++) {
        n[i] = nesCharToNibble(code[i]);
        if (n[i] < 0) return false;
    }

    // Address bits — same layout for both 6 and 8 char codes.
    // The high bit of n[3] is the address bit 14 (top of 15-bit address).
    uint16_t addr = 0x8000 +
                    (((n[3] & 7) << 12) |
                     ((n[5] & 7) << 8) | ((n[4] & 8) << 8) |
                     ((n[2] & 7) << 4) | ((n[1] & 8) << 4) |
                     (n[4] & 7)        | (n[3] & 8));
    // Value bits
    uint8_t val = ((n[1] & 7) << 4) | ((n[0] & 8) << 4) |
                  (n[0] & 7)        | (n[5] & 8);

    *outAddr = addr;
    *outVal = val;
    if (len == 8) {
        // Compare byte (8-char only) — used for codes targeting banked ROM
        uint8_t cmp = ((n[7] & 7) << 4) | ((n[6] & 8) << 4) |
                      (n[6] & 7)        | (n[5] & 8);
        // Per the spec, value's bit 3 actually comes from n[7] in 8-char codes
        val = ((n[1] & 7) << 4) | ((n[0] & 8) << 4) |
              (n[0] & 7)        | (n[7] & 8);
        *outVal = val;
        *outCmp = cmp;
        *outHasCmp = true;
    } else {
        *outCmp = 0;
        *outHasCmp = false;
    }
    return true;
}

// High-level: parse a Game Genie code string and add it to the active list.
inline bool addNESCode(const char* code, const char* label) {
    uint16_t addr;
    uint8_t val, cmp;
    bool hasCmp;
    if (!decodeNES(code, &addr, &val, &cmp, &hasCmp)) return false;
    bool ok = hasCmp ? addCompare(SYS_NES, addr, val, cmp, label)
                     : add(SYS_NES, addr, val, label);
    if (ok && g_activeCount > 0) {
        // Store the original code string in the new entry so saveCheats
        // can write it back as a parseable line.
        strncpy(g_active[g_activeCount - 1].code, code, sizeof(g_active[0].code) - 1);
        g_active[g_activeCount - 1].code[sizeof(g_active[0].code) - 1] = '\0';
    }
    return ok;
}

// ────────────────────────────────────────────────────────────────────────
// GAME-SPECIFIC HARDCODED PACKS
// ────────────────────────────────────────────────────────────────────────
// Detected by ROM header content during load. Each pack adds 1-4 famous
// codes that are universally fun without breaking the game.

inline void applyNESPack(const uint8_t* romHeader16) {
    // Check the iNES header — first 4 bytes are "NES\x1A".
    if (memcmp(romHeader16, "NES\x1A", 4) != 0) return;
    // For now we don't have header-based game detection (would need a
    // hash table of cartridge IDs). Game-specific NES cheats are best
    // applied via the future code-input UI, not auto-detection.
}

inline void applyGBPack(const uint8_t* bank0, uint32_t bank0Size) {
    // GB header title at 0x134, 11-15 chars depending on layout.
    if (bank0Size < 0x150) return;

    // Pokemon Red/Blue: infinite Master Ball, max money on cart purchase
    // (these are real famous codes that don't break the storyline)
    if (memcmp(bank0 + 0x134, "POKEMON RED", 11) == 0
        || memcmp(bank0 + 0x134, "POKEMON BLUE", 12) == 0) {
        // The Pokemon Red e-ink patches already shipped in v1.0 cover the
        // gameplay-affecting tweaks (instant text, fast saves). The game
        // genie codes are NOT applied automatically — too easy to ruin
        // someone's playthrough. Player must opt in via the future UI.
        // Leaving this empty as a documentation hook for now.
        return;
    }

    // Tetris: starting score offset / level (no famous starter codes worth
    // hardcoding — Tetris is already perfect)
    if (memcmp(bank0 + 0x134, "TETRIS", 6) == 0) {
        return;
    }
}

inline void applyForRom(SystemType sys, const uint8_t* romPtr, uint32_t romSize) {
    // Always start from a clean slate — previous game's cheats don't carry over.
    clearAll();
    if (sys == SYS_NES && romPtr && romSize >= 16) {
        applyNESPack(romPtr);
    } else if (sys == SYS_GAMEBOY && romPtr && romSize >= 0x150) {
        applyGBPack(romPtr, romSize);
    }
}

// ────────────────────────────────────────────────────────────────────────
// PERSISTENCE — one code per line in /games/cheats/{rom}.cht
// ────────────────────────────────────────────────────────────────────────
// File format is plain text, very forgiving:
//
//   # Optional comment lines starting with #
//   AAAA-BBB    Master Ball at start
//   GGAEZA       Infinite lives
//
// Lines are: code [whitespace] optional label. Codes are 6-9 chars,
// hyphen-separated chunks ignored. Empty lines and # comments skipped.

// Strip whitespace + hyphens from a Game Genie code, return cleaned length.
inline int normalizeCode(const char* in, char* out, int outSize) {
    int n = 0;
    for (int i = 0; in[i] && n < outSize - 1; i++) {
        char c = in[i];
        if (c == ' ' || c == '\t' || c == '-' || c == '_') continue;
        if (c >= 'a' && c <= 'z') c -= 32;  // Uppercase
        out[n++] = c;
    }
    out[n] = '\0';
    return n;
}

// Remove the cheat at index N from the active list (compacts the array).
inline void removeAt(int idx) {
    if (idx < 0 || idx >= g_activeCount) return;
    for (int i = idx; i < g_activeCount - 1; i++) {
        g_active[i] = g_active[i + 1];
    }
    g_activeCount--;
    g_active[g_activeCount].active = false;
}

// ────────────────────────────────────────────────────────────────────────
// GAME BOY GAME GENIE — 9-character format
// ────────────────────────────────────────────────────────────────────────
// Format: VVV-AAA-CCC where:
//   VVV = 3 hex chars: replacement value
//   AAA = 3 hex chars: address (low + mid bytes)
//   CCC = 3 hex chars: 1 char unused, 2 chars old value (compare)
// Old value is XOR'd with 0xBA then bit-rotated. We don't replicate the
// full obfuscation here — just the address+value extraction, which is
// enough for codes that target ROM addresses (the common case).
inline bool addGBCode(const char* code, const char* label) {
    char clean[16];
    int n = normalizeCode(code, clean, sizeof(clean));
    if (n != 9 && n != 6) return false;

    auto h = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int d[9];
    for (int i = 0; i < n; i++) {
        d[i] = h(clean[i]);
        if (d[i] < 0) return false;
    }

    // Value = first 3 hex digits combined into 8 bits (top nibble + bottom nibble)
    uint8_t value = (d[0] << 4) | d[1];
    // Address = next 3 digits (low) + 4th digit (high nibble), with bit 15 inverted
    uint16_t addr = (d[5] << 12) | (d[2] << 8) | (d[3] << 4) | d[4];
    addr ^= 0xF000;  // Bit-rotate fix per spec

    if (!add(SYS_GAMEBOY, addr, value, label)) return false;
    // Preserve the original code string so saveCheats() can round-trip
    // this entry back to disk. Without this, GB cheats loaded from file
    // wouldn't get persisted on the next save.
    strncpy(g_active[g_activeCount - 1].code, clean, sizeof(g_active[0].code) - 1);
    g_active[g_activeCount - 1].code[sizeof(g_active[0].code) - 1] = '\0';
    return true;
}

// ────────────────────────────────────────────────────────────────────────
// GAMESHARK — direct RAM modification (GB only for now)
// ────────────────────────────────────────────────────────────────────────
// Format: 8 hex chars = AABBCCDD
//   AA = RAM bank (00 for non-banked WRAM)
//   BB = value to write
//   CCDD = address (little-endian)
//
// GameShark codes write directly to RAM every frame, so they're not
// caught by ROM read interception. Instead we store them as "ram pokes"
// applied at the START of each frame in main loop. For simplicity,
// only WRAM/HRAM addresses (0xC000-0xFFFE) are supported — bank-switched
// WRAM/SRAM would need more plumbing.
struct RamPoke {
    SystemType system;
    uint16_t address;
    uint8_t value;
    bool active;
};
inline RamPoke g_pokes[16] = {};
inline int g_pokeCount = 0;

inline bool addGameSharkGB(const char* code, const char* label) {
    char clean[16];
    int n = normalizeCode(code, clean, sizeof(clean));
    if (n != 8) return false;

    auto h = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int d[8];
    for (int i = 0; i < 8; i++) {
        d[i] = h(clean[i]);
        if (d[i] < 0) return false;
    }
    // bank = d[0..1], value = d[2..3], addr = d[4..7] little-endian
    uint8_t value = (d[2] << 4) | d[3];
    uint16_t addrLo = (d[6] << 4) | d[7];
    uint16_t addrHi = (d[4] << 4) | d[5];
    uint16_t addr = (addrHi << 8) | addrLo;

    // Only WRAM/HRAM addresses are supported in this minimal impl
    if (addr < 0xC000) return false;

    if (g_pokeCount >= 16) return false;
    g_pokes[g_pokeCount++] = { SYS_GAMEBOY, addr, value, true };
    (void)label;  // Label storage not needed for ram pokes
    return true;
}

inline void clearAllPokes() {
    for (int i = 0; i < 16; i++) g_pokes[i].active = false;
    g_pokeCount = 0;
}

// ────────────────────────────────────────────────────────────────────────
// FILE I/O — /games/cheats/{romName}.cht persistence
// ────────────────────────────────────────────────────────────────────────
// Reads/writes a plain-text file containing one cheat per line. This
// lets users keep their curated Game Genie lists around between sessions,
// which is pretty much required for anything beyond the few famous codes
// the built-in packs auto-apply. The file format is intentionally simple
// so users can hand-edit on a PC:
//
//   # Metroid — infinite missiles
//   GXXPZA  Infinite missiles
//   # Super Mario Bros — infinite lives
//   SXIOPO  Inf lives
//
// The label is optional; anything after whitespace that follows the code
// is stored as the display name. Comment lines (starting with #) and
// empty lines are skipped during load.

// Build the /games/cheats/{rom}.cht path into a caller-supplied buffer.
inline void buildCheatsPath(const char* romName, char* out, int outSize) {
    snprintf(out, outSize, "/games/cheats/%s.cht", romName ? romName : "");
}

// Save the currently-active cheat list for the given ROM. Writes through
// a .tmp + rename dance so an interrupted save doesn't leave a partial
// file behind. Returns the number of cheats written, or -1 on I/O error.
inline int saveCheats(SdFat& sd, SystemType sys, const char* romName) {
    if (!romName || !romName[0]) return -1;
    if (!sd.exists("/games")) sd.mkdir("/games");
    if (!sd.exists("/games/cheats")) sd.mkdir("/games/cheats");

    char path[96]; buildCheatsPath(romName, path, sizeof(path));
    char tmp[100]; snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FsFile f;
    if (!f.open(tmp, O_WRONLY | O_CREAT | O_TRUNC)) return -1;

    // Simple header so hand-editors know what they're looking at
    f.print("# SumiBoy cheat codes — one per line, optional label after space\n");
    int written = 0;
    for (int i = 0; i < g_activeCount; i++) {
        const Cheat& c = g_active[i];
        if (!c.active) continue;
        // Only codes we can round-trip get persisted — anything added
        // from a built-in pack without a code string gets skipped, since
        // we can't re-parse an empty code next session.
        if (c.code[0] == '\0') continue;
        if (c.system != sys) continue;
        f.print(c.code);
        if (c.label[0]) { f.print(' '); f.print(c.label); }
        f.print('\n');
        written++;
    }
    f.sync();
    f.close();

    // 3-rename rotation (audit-aware audit upgrade): same logic
    // as gb_emulator_base.h's safeRename. Pre-fix used a `remove +
    // rename` pair with a window where the canonical file didn't
    // exist; a power loss there lost the cheat list entirely.
    char bak[100]; snprintf(bak, sizeof(bak), "%s.bak", path);
    if (sd.exists(bak)) sd.remove(bak);
    const bool hadCanonical = sd.exists(path);
    if (hadCanonical && !sd.rename(path, bak)) {
        sd.remove(tmp);
        return -1;
    }
    if (!sd.rename(tmp, path)) {
        if (hadCanonical) sd.rename(bak, path);  // roll back
        sd.remove(tmp);
        return -1;
    }
    if (hadCanonical) sd.remove(bak);
    return written;
}

// Load the cheats file for a ROM and re-populate the active list. Caller
// is responsible for calling clearAll() first if they want a fresh start.
// Returns the number of cheats successfully parsed (including ones that
// the decoder rejected as invalid — those count as parse attempts).
inline int loadCheats(SdFat& sd, SystemType sys, const char* romName) {
    if (!romName || !romName[0]) return 0;
    char path[96]; buildCheatsPath(romName, path, sizeof(path));
    if (!sd.exists(path)) return 0;

    FsFile f;
    if (!f.open(path, O_RDONLY)) return 0;

    int added = 0;
    char line[96];
    while (true) {
        int n = f.fgets(line, sizeof(line));
        if (n <= 0) break;
        // Strip trailing \r\n
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ')) {
            line[--n] = '\0';
        }
        if (n == 0) continue;
        if (line[0] == '#') continue;  // Comment

        // Split into code + label at first whitespace
        char* code = line;
        char* label = nullptr;
        for (int i = 0; line[i]; i++) {
            if (line[i] == ' ' || line[i] == '\t') {
                line[i] = '\0';
                label = line + i + 1;
                while (*label == ' ' || *label == '\t') label++;
                break;
            }
        }
        if (code[0] == '\0') continue;

        bool ok = false;
        if (sys == SYS_NES) {
            ok = addNESCode(code, label && *label ? label : "file");
        } else if (sys == SYS_GAMEBOY) {
            // Try Game Boy Game Genie first, then GameShark fall-through
            ok = addGBCode(code, label && *label ? label : "file");
            if (!ok) ok = addGameSharkGB(code, label && *label ? label : "file");
        }
        if (ok) added++;
    }
    f.close();
    return added;
}

}  // namespace cheats
