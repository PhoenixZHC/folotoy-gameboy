#include "gb_display.h"

#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gb_pixels.h"
#include "ui_font.h"
#include <ctype.h>
#include <string.h>

#define STRIP_ROWS 20
static uint16_t *s_strip;
static SemaphoreHandle_t s_done;
static bool s_ready;
static uint8_t s_previous_frame[GB_FRAME_BYTES];
static bool s_previous_valid;
static bool s_previous_enlarged;
static uint8_t s_queued_frame[GB_FRAME_BYTES];
static SemaphoreHandle_t s_frame_free;
static TaskHandle_t s_frame_task;
static bool s_queued_enlarged;
static esp_err_t s_frame_error = ESP_OK;

static esp_err_t wait_frame_idle(void) {
    if (!s_frame_task) return ESP_OK;
    if (xSemaphoreTake(s_frame_free, pdMS_TO_TICKS(1000)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    xSemaphoreGive(s_frame_free);
    return s_frame_error;
}

static void frame_task(void *arg) {
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        esp_err_t e = gb_display_frame(s_queued_frame, s_queued_enlarged);
        if (e != ESP_OK) s_frame_error = e;
        xSemaphoreGive(s_frame_free);
    }
}

static bool transfer_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *event, void *context) {
    (void)io;
    (void)event;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &woken);
    return woken == pdTRUE;
}

static esp_err_t send_strip(int x, int y, int width, int rows) {
    esp_err_t e = esp_lcd_panel_draw_bitmap(bsp_display_panel(), x, y,
                                           x + width, y + rows, s_strip);
    if (e != ESP_OK) return e;
    return xSemaphoreTake(s_done, pdMS_TO_TICKS(1000)) == pdTRUE
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t gb_display_init(void) {
    if (s_ready) return ESP_OK;
    esp_err_t e = bsp_display_init();
    if (e != ESP_OK) return e;
    s_strip = heap_caps_malloc(BSP_LCD_W * STRIP_ROWS * sizeof(uint16_t),
                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_done = xSemaphoreCreateBinary();
    if (!s_strip || !s_done) {
        if (s_strip) heap_caps_free(s_strip);
        if (s_done) vSemaphoreDelete(s_done);
        s_strip = NULL;
        s_done = NULL;
        return ESP_ERR_NO_MEM;
    }
    const esp_lcd_panel_io_callbacks_t cb = {.on_color_trans_done = transfer_done};
    e = esp_lcd_panel_io_register_event_callbacks(bsp_display_io(), &cb, s_done);
    if (e != ESP_OK) {
        heap_caps_free(s_strip);
        vSemaphoreDelete(s_done);
        s_strip = NULL;
        s_done = NULL;
        return e;
    }
    s_ready = true;
    e = gb_display_clear(0x0000);
    if (e != ESP_OK) return e;
    bsp_display_backlight(100);
    s_frame_free = xSemaphoreCreateBinary();
    if (!s_frame_free) return ESP_ERR_NO_MEM;
    xSemaphoreGive(s_frame_free);
    if (xTaskCreate(frame_task, "gb_display", 4096, NULL, 2, &s_frame_task) != pdPASS) {
        vSemaphoreDelete(s_frame_free);
        s_frame_free = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t gb_display_clear(uint16_t rgb565) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t idle = wait_frame_idle();
    if (idle != ESP_OK) return idle;
    s_previous_valid = false;
    const uint16_t wire = __builtin_bswap16(rgb565);
    for (int row = 0; row < BSP_LCD_H; row += STRIP_ROWS) {
        int count = BSP_LCD_H - row;
        if (count > STRIP_ROWS) count = STRIP_ROWS;
        for (int i = 0; i < BSP_LCD_W * count; i++) s_strip[i] = wire;
        esp_err_t e = send_strip(0, row, BSP_LCD_W, count);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}

esp_err_t gb_display_screen_on(bool on) {
    esp_err_t e = wait_frame_idle();
    if (e != ESP_OK) return e;
    return esp_lcd_panel_disp_on_off(bsp_display_panel(), on);
}

esp_err_t gb_display_boot_frame(const uint8_t *frame) {
    esp_err_t e = wait_frame_idle();
    if (e != ESP_OK) return e;
    for (int row = 0; row < 216; row += STRIP_ROWS) {
        int count = 216 - row;
        if (count > STRIP_ROWS) count = STRIP_ROWS;
        if (s_previous_valid && gb_pixels_strip_changed(frame, s_previous_frame,
                                                        row, count, true) == false) continue;
        for (int y = 0; y < count; y++) for (int x = 0; x < BSP_LCD_W; x++) {
            int sy = (row + y) * 2 / 3;
            bool dark = gb_pixel_shade(frame, x * 2 / 3, sy);
            s_strip[y * BSP_LCD_W + x] = __builtin_bswap16(dark ? GB_LCD_DARK : GB_LCD_LIGHT);
        }
        e = send_strip(0, row + 52, BSP_LCD_W, count);
        if (e != ESP_OK) { s_previous_valid = false; return e; }
    }
    memcpy(s_previous_frame, frame, GB_FRAME_BYTES);
    s_previous_valid = true;
    s_previous_enlarged = true;
    return ESP_OK;
}

esp_err_t gb_display_frame(const uint8_t *frame, bool enlarged) {
    if (!s_ready || !frame) return ESP_ERR_INVALID_ARG;
    const int width = enlarged ? 240 : GB_WIDTH;
    const int height = enlarged ? 216 : GB_HEIGHT;
    const int x0 = (BSP_LCD_W - width) / 2;
    const int y0 = (BSP_LCD_H - height) / 2;
    for (int row = 0; row < height; row += STRIP_ROWS) {
        int count = height - row;
        if (count > STRIP_ROWS) count = STRIP_ROWS;
        if (s_previous_valid && s_previous_enlarged == enlarged &&
            !gb_pixels_strip_changed(frame, s_previous_frame, row, count, enlarged)) continue;
        int previous_sy = -1;
        for (int dy = 0; dy < count; dy++) {
            int sy = gb_pixel_source_y(row + dy, enlarged);
            uint16_t *out = s_strip + dy * width;
            if (enlarged) {
                if (sy == previous_sy) memcpy(out, out - width, width * sizeof(uint16_t));
                else gb_pixels_expand_row_3_2(frame + sy * (GB_WIDTH / 4), out);
            } else {
                for (int dx = 0; dx < width; dx++) {
                    uint16_t color = gb_pixel_rgb565(gb_pixel_shade(frame, dx, sy));
                    out[dx] = __builtin_bswap16(color);
                }
            }
            previous_sy = sy;
        }
        esp_err_t e = send_strip(x0, y0 + row, width, count);
        if (e != ESP_OK) { s_previous_valid = false; return e; }
    }
    memcpy(s_previous_frame, frame, sizeof(s_previous_frame));
    s_previous_enlarged = enlarged;
    s_previous_valid = true;
    return ESP_OK;
}

esp_err_t gb_display_color_frame(const uint8_t *indices,
                                 const uint8_t *bg_palette, const uint8_t *obj_palette) {
    if (!s_ready || !indices || !bg_palette || !obj_palette) return ESP_ERR_INVALID_ARG;
    esp_err_t idle = wait_frame_idle();
    if (idle != ESP_OK) return idle;
    s_previous_valid = false;
    uint16_t colors[64];
    for (int i = 0; i < 64; i++) {
        const uint8_t *palette = i < 32 ? bg_palette : obj_palette;
        int offset = (i % 32) * 2;
        uint16_t rgb555 = palette[offset] | ((uint16_t)palette[offset + 1] << 8);
        uint16_t r = rgb555 & 31;
        uint16_t g = (rgb555 >> 5) & 31;
        uint16_t b = (rgb555 >> 10) & 31;
        uint16_t rgb565 = (r << 11) | (((g << 1) | (g >> 4)) << 5) | b;
        colors[i] = __builtin_bswap16(rgb565);
    }
    for (int row = 0; row < 216; row += STRIP_ROWS) {
        int rows = 216 - row;
        if (rows > STRIP_ROWS) rows = STRIP_ROWS;
        for (int dy = 0; dy < rows; dy++) {
            int sy = (row + dy) * 2 / 3;
            uint16_t *out = s_strip + dy * 240;
            const uint8_t *src = indices + sy * GB_WIDTH;
            for (int x = 0; x < 240; x++) out[x] = colors[src[x * 2 / 3] & 63];
        }
        esp_err_t e = send_strip(0, 52 + row, 240, rows);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}

esp_err_t gb_display_submit_frame(const uint8_t *frame, bool enlarged, bool *queued) {
    if (queued) *queued = false;
    if (!s_ready || !s_frame_task || !frame || !queued) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_frame_free, 0) != pdTRUE) return ESP_OK;
    if (s_frame_error != ESP_OK) {
        xSemaphoreGive(s_frame_free);
        return s_frame_error;
    }
    memcpy(s_queued_frame, frame, sizeof(s_queued_frame));
    s_queued_enlarged = enlarged;
    *queued = true;
    xTaskNotifyGive(s_frame_task);
    return ESP_OK;
}

static const uint8_t s_digits[10][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
};
static const uint8_t s_letters[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x7f,0x20,0x18,0x20,0x7f}, {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43},
};

static uint8_t glyph_column(char character, int column) {
    unsigned char c = (unsigned char)toupper((unsigned char)character);
    if (c >= '0' && c <= '9') return s_digits[c - '0'][column];
    if (c >= 'A' && c <= 'Z') return s_letters[c - 'A'][column];
    if (c == '-') return column == 2 ? 0x08 : 0;
    if (c == '_') return column < 5 ? 0x40 : 0;
    if (c == '.') return column == 2 ? 0x40 : 0;
    if (c == ':') return column == 2 ? 0x24 : 0;
    if (c == '%') return column == 0 ? 0x63 : column == 1 ? 0x13 :
                         column == 2 ? 0x08 : column == 3 ? 0x64 : 0x63;
    if (c == '?') return column == 0 ? 0x02 : column == 1 ? 0x01 :
                         column == 2 ? 0x51 : column == 3 ? 0x09 : 0x06;
    if (c == '>') return column == 1 ? 0x22 : column == 2 ? 0x14 : column == 3 ? 0x08 : 0;
    if (c == '/') return (uint8_t)(0x40 >> column);
    return 0;
}

esp_err_t gb_display_lines(const char *const *lines, size_t count) {
    if (!s_ready || !lines) return ESP_ERR_INVALID_ARG;
    esp_err_t idle = wait_frame_idle();
    if (idle != ESP_OK) return idle;
    s_previous_valid = false;
    const int top = 24;
    for (int row = 0; row < BSP_LCD_H; row += STRIP_ROWS) {
        int rows = BSP_LCD_H - row;
        if (rows > STRIP_ROWS) rows = STRIP_ROWS;
        memset(s_strip, 0, BSP_LCD_W * rows * sizeof(uint16_t));
        for (int dy = 0; dy < rows; dy++) {
            int yy = row + dy - top;
            if (yy < 0 || (size_t)(yy / 16) >= count || yy % 16 >= 7) continue;
            const char *line = lines[yy / 16];
            if (!line) continue;
            for (int x = 0; x < 38 && line[x]; x++) {
                int xx = 6 + x * 6;
                for (int col = 0; col < 5; col++) {
                    if (glyph_column(line[x], col) & (1u << (yy % 16)))
                        s_strip[dy * BSP_LCD_W + xx + col] = 0xffff;
                }
            }
        }
        esp_err_t e = send_strip(0, row, BSP_LCD_W, rows);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}

static void ui_fill(int strip_y, int rows, int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (x + w > BSP_LCD_W) w = BSP_LCD_W - x;
    if (w <= 0) return;
    int from = y > strip_y ? y : strip_y;
    int to = y + h < strip_y + rows ? y + h : strip_y + rows;
    for (int py = from; py < to; py++) {
        uint16_t *out = s_strip + (py - strip_y) * BSP_LCD_W + x;
        for (int px = 0; px < w; px++) out[px] = color;
    }
}

static void ui_frame(int strip_y, int rows, int x, int y, int w, int h,
                     int border, uint16_t color) {
    ui_fill(strip_y, rows, x, y, w, border, color);
    ui_fill(strip_y, rows, x, y + h - border, w, border, color);
    ui_fill(strip_y, rows, x, y, border, h, color);
    ui_fill(strip_y, rows, x + w - border, y, border, h, color);
}

static uint32_t ui_next_codepoint(const unsigned char **cursor) {
    const unsigned char *s = *cursor;
    uint32_t cp = *s++;
    if (cp >= 0xF0 && s[0] && s[1] && s[2]) {
        cp = ((cp & 7) << 18) | ((s[0] & 63) << 12) |
             ((s[1] & 63) << 6) | (s[2] & 63); s += 3;
    } else if (cp >= 0xE0 && s[0] && s[1]) {
        cp = ((cp & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2;
    } else if (cp >= 0xC0 && s[0]) {
        cp = ((cp & 31) << 6) | (s[0] & 63); s++;
    }
    *cursor = s;
    return cp;
}

static void ui_text_region(int strip_y, int rows, int x, int y, int min_x, int max_x,
                           const char *text, uint16_t color) {
    if (!text) return;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor && x < max_x) {
        uint32_t cp = ui_next_codepoint(&cursor);
        const uint16_t *glyph = cp > 127 ? ui_font_rows(cp) : NULL;
        int width = cp > 127 ? 16 : 12;
        for (int py = 0; py < 16; py++) {
            int yy = y + py;
            if (yy < strip_y || yy >= strip_y + rows) continue;
            for (int px = 0; px < width && x + px < max_x; px++) {
                bool on = false;
                if (glyph) {
                    on = (glyph[py] & (0x8000u >> px)) != 0;
                } else if (cp > 127) on = py == 0 || py == 15 || px == 0 || px == width - 1;
                else if (px < 10 && py >= 1 && py < 15) {
                    int col = px / 2, row = (py - 1) / 2;
                    on = (glyph_column((char)cp, col) & (1u << row)) != 0;
                }
                if (on && x + px >= min_x) s_strip[(yy - strip_y) * BSP_LCD_W + x + px] = color;
            }
        }
        x += width + 1;
    }
}

static void ui_text(int strip_y, int rows, int x, int y, int max_x,
                    const char *text, uint16_t color) {
    ui_text_region(strip_y, rows, x, y, 0, max_x, text, color);
}

static int ui_text_width(const char *text) {
    int width = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) width += ui_next_codepoint(&cursor) > 127 ? 17 : 13;
    return width;
}

esp_err_t gb_display_footer(const char *text, uint32_t scroll_px) {
    if (!s_ready || !text) return ESP_ERR_INVALID_ARG;
    esp_err_t idle = wait_frame_idle();
    if (idle != ESP_OK) return idle;
    const uint16_t paper = __builtin_bswap16(GB_LCD_LIGHT);
    const uint16_t ink = __builtin_bswap16(GB_LCD_DARK);
    int width = ui_text_width(text);
    int offset = width > 220 ? (int)(scroll_px % (uint32_t)(width + 28)) : 0;
    for (int row = 289; row < BSP_LCD_H; row += STRIP_ROWS) {
        int rows = BSP_LCD_H - row;
        if (rows > STRIP_ROWS) rows = STRIP_ROWS;
        ui_fill(row, rows, 0, row, BSP_LCD_W, rows, ink);
        ui_fill(row, rows, 6, 293, 4, 23, __builtin_bswap16(GB_LCD_MID));
        ui_text_region(row, rows, 13 - offset, 299, 13, 233, text, paper);
        if (offset && width > 220)
            ui_text_region(row, rows, 13 + width + 28 - offset, 299,
                           13, 233, text, paper);
        esp_err_t e = send_strip(0, row, BSP_LCD_W, rows);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}

esp_err_t gb_display_game_hint(const char *text) {
    if (!s_ready || !text) return ESP_ERR_INVALID_ARG;
    esp_err_t idle = wait_frame_idle();
    if (idle != ESP_OK) return idle;
    const uint16_t bg = __builtin_bswap16(GB_LCD_MID);
    const uint16_t ink = __builtin_bswap16(GB_LCD_DARK);
    const uint16_t accent = __builtin_bswap16(GB_LCD_SHADE);
    for (int row = 272; row < BSP_LCD_H; row += STRIP_ROWS) {
        int rows = BSP_LCD_H - row;
        if (rows > STRIP_ROWS) rows = STRIP_ROWS;
        ui_fill(row, rows, 0, row, BSP_LCD_W, rows, bg);
        ui_fill(row, rows, 0, 272, BSP_LCD_W, 2, accent);
        ui_text(row, rows, 10, 289, 232, text, ink);
        esp_err_t e = send_strip(0, row, BSP_LCD_W, rows);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}

esp_err_t gb_display_ui(const gb_ui_model_t *model) {
    if (!s_ready || !model || model->count > GB_UI_MAX_ITEMS) return ESP_ERR_INVALID_ARG;
    esp_err_t idle = wait_frame_idle();
    if (idle != ESP_OK) return idle;
    s_previous_valid = false;
    const uint16_t bg = __builtin_bswap16(GB_LCD_LIGHT);
    const uint16_t ink = __builtin_bswap16(GB_LCD_DARK);
    const uint16_t paper = __builtin_bswap16(GB_LCD_LIGHT);
    const uint16_t mid = __builtin_bswap16(GB_LCD_MID);
    const uint16_t shade = __builtin_bswap16(GB_LCD_SHADE);
    for (int row = 0; row < BSP_LCD_H; row += STRIP_ROWS) {
        int rows = BSP_LCD_H - row;
        if (rows > STRIP_ROWS) rows = STRIP_ROWS;
        ui_fill(row, rows, 0, row, BSP_LCD_W, rows, bg);
        ui_frame(row, rows, 2, 2, 236, 286, 2, ink);
        ui_fill(row, rows, 6, 41, 228, 3, ink);
        ui_fill(row, rows, 9, 8, 4, 23, shade);
        ui_text(row, rows, 17, 13, 180, model->title, ink);
        ui_frame(row, rows, 199, 6, 29, 16, 2, ink);
        ui_fill(row, rows, 228, 10, 3, 8, ink);
        if (model->battery_percent >= 0) {
            int percent = model->battery_percent > 100 ? 100 : model->battery_percent;
            int fill = percent * 23 / 100;
            ui_fill(row, rows, 202, 9, fill, 10, percent < 20 ? shade : ink);
            char level[16];
            snprintf(level, sizeof(level), "%d%%", percent);
            ui_text(row, rows, 180, 24, 239, level, ink);
        } else ui_text(row, rows, 209, 24, 239, "?", ink);
        for (size_t i = 0; i < model->count; i++) {
            int y = 54 + (int)i * 42;
            bool active = !model->compact && i == model->selected;
            ui_fill(row, rows, 10, y, 220, 37, active ? ink : mid);
            ui_frame(row, rows, 10, y, 220, 37, 2, active ? ink : shade);
            if (model->compact) {
                ui_fill(row, rows, 16, y + 15, 7, 7, ink);
                ui_text_region(row, rows, 29, y + 10, 29, 224,
                               model->items[i], ink);
            } else {
                ui_fill(row, rows, 17, y + 8, 20, 20, active ? paper : shade);
                ui_text(row, rows, 21, y + 10, 36, active ? ">" : "-", active ? ink : paper);
                ui_text(row, rows, 45, y + 10, 224, model->items[i], active ? paper : ink);
            }
        }
        ui_fill(row, rows, 9, 264, 222, 21, mid);
        ui_text(row, rows, 12, 268, 230, model->hint, ink);
        ui_fill(row, rows, 0, 289, 240, 31, ink);
        ui_fill(row, rows, 6, 293, 4, 23, mid);
        ui_text(row, rows, 13, 299, 233, model->footer, paper);
        esp_err_t e = send_strip(0, row, BSP_LCD_W, rows);
        if (e != ESP_OK) return e;
    }
    return ESP_OK;
}
