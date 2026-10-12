#pragma once
#include <stdbool.h>
#include <stdint.h>

// Unified input before the emulator's dead zone and A/B-only mapping.
typedef struct {
    bool connected, keyboard, iine;
    uint16_t firmware;
    uint16_t buttons; // A B X Y L R ZL ZR L3 R3 Select Start System Capture
    uint8_t dpad; // Up Down Right Left
    int32_t axes[4]; // LX LY RX RY, nominal -512..511
    int32_t triggers[2]; // 0..1023; digital triggers may only report endpoints.
    uint32_t reports;
    uint32_t pressed_at[18];
    uint32_t seen_press;
} gamepad_test_state_t;

void gamepad_test_reset(gamepad_test_state_t *state, bool connected, bool keyboard);
void gamepad_test_update(gamepad_test_state_t *state, uint16_t buttons, uint8_t dpad,
                         const int32_t axes[4], const int32_t triggers[2], uint32_t now);
int gamepad_test_axis(int32_t value, int radius);
bool gamepad_test_recent(const gamepad_test_state_t *state, unsigned bit, uint32_t now);

typedef struct {
    void *context;
    void (*fill)(void *context, int x, int y, int w, int h, uint16_t color);
    void (*text)(void *context, int x, int y, const char *text, uint16_t color, bool small);
} gamepad_test_canvas_t;
// Uses the firmware's four-color palette; can render directly into LCD stripes.
void gamepad_test_render(const gamepad_test_state_t *state, uint32_t now, int battery,
                          const gamepad_test_canvas_t *canvas);
