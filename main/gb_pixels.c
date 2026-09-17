#include "gb_pixels.h"
#include <string.h>

uint8_t gb_pixel_shade(const uint8_t *frame, int x, int y) {
    return (frame[y * (GB_WIDTH / 4) + x / 4] >> ((x % 4) * 2)) & 3;
}

uint16_t gb_pixel_rgb565(uint8_t shade) {
    static const uint16_t colors[4] = {0xffff, 0xbdd7, 0x7bef, 0x0000};
    return colors[shade & 3];
}

int gb_pixel_source_x(int dest_x, bool enlarged) {
    return enlarged ? dest_x * GB_WIDTH / 240 : dest_x;
}

int gb_pixel_source_y(int dest_y, bool enlarged) {
    return enlarged ? dest_y * GB_HEIGHT / 216 : dest_y;
}

void gb_pixels_expand_row_3_2(const uint8_t packed[GB_WIDTH / 4], uint16_t wire[240]) {
    const uint16_t colors[4] = {
        __builtin_bswap16(gb_pixel_rgb565(0)),
        __builtin_bswap16(gb_pixel_rgb565(1)),
        __builtin_bswap16(gb_pixel_rgb565(2)),
        __builtin_bswap16(gb_pixel_rgb565(3)),
    };
    for (int i = 0; i < GB_WIDTH / 4; i++) {
        uint8_t pixels = packed[i];
        uint16_t *out = wire + i * 6;
        out[0] = out[1] = colors[pixels & 3];
        out[2] = colors[(pixels >> 2) & 3];
        out[3] = out[4] = colors[(pixels >> 4) & 3];
        out[5] = colors[pixels >> 6];
    }
}

bool gb_pixels_strip_changed(const uint8_t *frame, const uint8_t *previous,
                             int first_dest_row, int dest_rows, bool enlarged) {
    const int first = gb_pixel_source_y(first_dest_row, enlarged);
    const int last = gb_pixel_source_y(first_dest_row + dest_rows - 1, enlarged);
    for (int sy = first; sy <= last; sy++) {
        const int offset = sy * (GB_WIDTH / 4);
        if (memcmp(frame + offset, previous + offset, GB_WIDTH / 4) != 0) return true;
    }
    return false;
}
