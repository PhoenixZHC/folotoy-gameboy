#include <assert.h>
#include <string.h>
#include "gb_pixels.h"

int main(void) {
    uint8_t frame[GB_FRAME_BYTES] = {0};
    frame[0] = 0xe4;
    for (int x = 0; x < 4; x++) assert(gb_pixel_shade(frame, x, 0) == x);
    assert(gb_pixel_rgb565(0) == 0xffff);
    assert(gb_pixel_rgb565(3) == 0);
    assert(gb_pixel_source_x(239, true) == 159);
    assert(gb_pixel_source_y(215, true) == 143);
    assert(gb_pixel_source_x(159, false) == 159);
    uint8_t row[GB_WIDTH / 4];
    uint16_t scaled[242];
    scaled[0] = 0x1234;
    scaled[241] = 0xabcd;
    for (int pattern = 0; pattern < 256; pattern++) {
        memset(row, pattern, sizeof(row));
        gb_pixels_expand_row_3_2(row, scaled + 1);
        for (int x = 0; x < 240; x++) {
            int source_x = gb_pixel_source_x(x, true);
            uint8_t shade = (row[source_x / 4] >> ((source_x % 4) * 2)) & 3;
            assert(scaled[x + 1] == __builtin_bswap16(gb_pixel_rgb565(shade)));
        }
    }
    for (int x = 0; x < (int)sizeof(row); x++) row[x] = (uint8_t)(x * 37 + 11);
    gb_pixels_expand_row_3_2(row, scaled + 1);
    for (int x = 0; x < 240; x++) {
        int source_x = gb_pixel_source_x(x, true);
        uint8_t shade = (row[source_x / 4] >> ((source_x % 4) * 2)) & 3;
        assert(scaled[x + 1] == __builtin_bswap16(gb_pixel_rgb565(shade)));
    }
    assert(scaled[0] == 0x1234 && scaled[241] == 0xabcd);
    uint8_t previous[GB_FRAME_BYTES] = {0};
    memset(frame, 0, sizeof(frame));
    assert(!gb_pixels_strip_changed(frame, previous, 0, 20, true));
    frame[0] = 1;
    assert(gb_pixels_strip_changed(frame, previous, 0, 20, true));
    assert(!gb_pixels_strip_changed(frame, previous, 20, 20, true));
    frame[0] = 0;
    frame[(GB_HEIGHT - 1) * (GB_WIDTH / 4)] = 1;
    assert(gb_pixels_strip_changed(frame, previous, 200, 16, true));
    assert(!gb_pixels_strip_changed(frame, previous, 0, 20, true));
    assert(gb_pixels_strip_changed(frame, previous, 143, 1, false));
    return 0;
}
