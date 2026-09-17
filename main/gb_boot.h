#pragma once
#include <stdint.h>
#include "gb_pixels.h"
void gb_boot_frame(uint8_t frame[GB_FRAME_BYTES], int top);
int16_t gb_boot_sample(uint32_t sample);
