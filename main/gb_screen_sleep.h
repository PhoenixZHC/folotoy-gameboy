#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { unsigned phase; uint32_t released_ms; bool released; } gb_screen_sleep_t;
// Require a fresh press, then consume it until release before returning to play.
static inline void gb_screen_sleep_step(gb_screen_sleep_t *s, bool pressed, uint32_t now) {
    if (s->phase == 1 && pressed) { s->phase = 2; s->released = false; }
    if (s->phase == 0 || s->phase == 2) {
        if (pressed) s->released = false;
        else if (!s->released) { s->released = true; s->released_ms = now; }
        else if ((uint32_t)(now - s->released_ms) >= 80) {
            s->phase++; s->released = false;
        }
    }
}
