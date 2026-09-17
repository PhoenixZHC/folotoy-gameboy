#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t codepoint;
    uint16_t rows[16];
} ui_glyph_t;

// NULL means unsupported; callers draw a visible replacement box.
const uint16_t *ui_font_rows(uint32_t codepoint);
