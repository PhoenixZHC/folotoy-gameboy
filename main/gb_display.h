#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Original monochrome Game Boy LCD palette, encoded as RGB565.
#define GB_LCD_LIGHT 0x9DC2
#define GB_LCD_MID   0x8D42
#define GB_LCD_SHADE 0x3306
#define GB_LCD_DARK  0x09C2

esp_err_t gb_display_init(void);
esp_err_t gb_display_screen_on(bool on);
esp_err_t gb_display_boot_frame(const uint8_t *frame);
esp_err_t gb_display_clear(uint16_t rgb565);
esp_err_t gb_display_frame(const uint8_t *packed_frame, bool enlarged);
esp_err_t gb_display_color_frame(const uint8_t *indices,
                                 const uint8_t *bg_palette, const uint8_t *obj_palette);
esp_err_t gb_display_submit_frame(const uint8_t *packed_frame, bool enlarged,
                                  bool *queued);
esp_err_t gb_display_lines(const char *const *lines, size_t count);
esp_err_t gb_display_game_hint(const char *text);
esp_err_t gb_display_footer(const char *text, uint32_t scroll_px);

#define GB_UI_MAX_ITEMS 5
typedef struct {
    const char *title;
    const char *hint;
    const char *items[GB_UI_MAX_ITEMS];
    size_t count;
    size_t selected;
    const char *footer;
    int battery_percent; // -1 when unavailable
    bool compact; // smaller text and no selection marker for information pages
} gb_ui_model_t;

esp_err_t gb_display_ui(const gb_ui_model_t *model);
