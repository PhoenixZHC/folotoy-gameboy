#include "gb_boot.h"
#include <string.h>

// Cartridge-header logo encoding; redraw the DMG sequence without a boot ROM.
static const uint8_t logo[48] = {
    0xce,0xed,0x66,0x66,0xcc,0x0d,0x00,0x0b,0x03,0x73,0x00,0x83,
    0x00,0x0c,0x00,0x0d,0x00,0x08,0x11,0x1f,0x88,0x89,0x00,0x0e,
    0xdc,0xcc,0x6e,0xe6,0xdd,0xdd,0xd9,0x99,0xbb,0xbb,0x67,0x63,
    0x6e,0x0e,0xec,0xcc,0xdd,0xdc,0x99,0x9f,0xbb,0xb9,0x33,0x3e
};
static void dot(uint8_t *frame, int x, int y) {
    if (x >= 0 && x < GB_WIDTH && y >= 0 && y < GB_HEIGHT)
        frame[y * (GB_WIDTH / 4) + x / 4] |= 3u << ((x % 4) * 2);
}
void gb_boot_frame(uint8_t frame[GB_FRAME_BYTES], int top) {
    memset(frame, 0, GB_FRAME_BYTES);
    for (int i = 0; i < 48; i++) {
        int x = 32 + (i / 2 % 12) * 8;
        int y = top + (i / 24) * 8 + (i % 2) * 4;
        for (int bit = 0; bit < 8; bit++) if (logo[i] & (0x80u >> bit)) {
            int dx = (bit % 4) * 2, dy = (bit / 4) * 2;
            dot(frame,x+dx,y+dy); dot(frame,x+dx+1,y+dy);
            dot(frame,x+dx,y+dy+1); dot(frame,x+dx+1,y+dy+1);
        }
    }
    static const uint8_t registered[8] = {0x3c,0x42,0xb9,0xa5,0xb9,0xa5,0x42,0x3c};
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        if (registered[y] & (0x80u >> x)) dot(frame,128+x,top+y);
}

// DMG two-note square-wave chime at 14 kHz; honour the saved volume externally.
int16_t gb_boot_sample(uint32_t sample) {
    const uint32_t first = 4 * 234, end = 4 * 234 + 15 * 656;
    if (sample >= end) return 0;
    uint32_t n = sample < first ? sample : sample - first;
    uint32_t period = sample < first ? 125 : 63;
    uint32_t level = 15 - (n / 656 > 15 ? 15 : n / 656);
    int amplitude = (int)level * 180;
    return ((uint64_t)n * 262144 / (14000 * period)) & 1 ? amplitude : -amplitude;
}
