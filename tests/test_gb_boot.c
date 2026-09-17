#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gb_boot.h"
#include "gb_screen_sleep.h"
int main(void) {
    uint8_t frame[GB_FRAME_BYTES];
    gb_boot_frame(frame, -36);
    for (unsigned i = 0; i < sizeof(frame); i++) assert(frame[i] == 0);
    gb_boot_frame(frame, 64);
    unsigned count = 0;
    for (int y = 0; y < GB_HEIGHT; y++) for (int x = 0; x < GB_WIDTH; x++) {
        if (gb_pixel_shade(frame,x,y)) {
            count++; assert(x >= 32 && x < 136 && y >= 64 && y < 80);
        }
    }
    assert(count > 400);
    assert(gb_boot_sample(0) != 0 && gb_boot_sample(936) != 0);
    assert(gb_boot_sample(10776) == 0 && gb_boot_sample(UINT32_MAX) == 0);
    gb_screen_sleep_t s = {0};
    gb_screen_sleep_step(&s,true,0); assert(s.phase == 0);
    gb_screen_sleep_step(&s,false,20);
    gb_screen_sleep_step(&s,false,99); assert(s.phase == 0);
    gb_screen_sleep_step(&s,false,100); assert(s.phase == 1);
    gb_screen_sleep_step(&s,true,120); assert(s.phase == 2);
    gb_screen_sleep_step(&s,true,1000); assert(s.phase == 2);
    gb_screen_sleep_step(&s,false,1020);
    gb_screen_sleep_step(&s,false,1100); assert(s.phase == 3);
    s = (gb_screen_sleep_t){0};
    gb_screen_sleep_step(&s,false,UINT32_MAX - 39);
    gb_screen_sleep_step(&s,false,40); assert(s.phase == 1);
    FILE *preview = fopen("boot-preview.pgm", "wb"); assert(preview);
    fprintf(preview,"P5\n160 144\n255\n");
    for (int y = 0; y < GB_HEIGHT; y++) for (int x = 0; x < GB_WIDTH; x++)
        fputc(gb_pixel_shade(frame,x,y) ? 0 : 255, preview);
    fclose(preview);
    puts("Boot and wake gating: PASS");
}
