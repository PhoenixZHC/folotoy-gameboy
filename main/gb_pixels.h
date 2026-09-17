#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GB_WIDTH 160
#define GB_HEIGHT 144
#define GB_FRAME_BYTES (GB_WIDTH * GB_HEIGHT / 4)

uint8_t gb_pixel_shade(const uint8_t *frame, int x, int y);
uint16_t gb_pixel_rgb565(uint8_t shade);
int gb_pixel_source_x(int dest_x, bool enlarged);
int gb_pixel_source_y(int dest_y, bool enlarged);
void gb_pixels_expand_row_3_2(const uint8_t packed[GB_WIDTH / 4], uint16_t wire[240]);
bool gb_pixels_strip_changed(const uint8_t *frame, const uint8_t *previous,
                             int first_dest_row, int dest_rows, bool enlarged);
