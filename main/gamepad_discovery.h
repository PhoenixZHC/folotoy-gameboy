#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAMEPAD_DISCOVERY_MAX 8

typedef struct {
    uint8_t address[6];
    char name[32];
    bool keyboard;
} gamepad_candidate_t;

typedef struct {
    gamepad_candidate_t candidates[GAMEPAD_DISCOVERY_MAX];
    size_t count;
    bool selected;
    uint8_t selected_address[6];
    bool passkey_visible;
    uint32_t passkey;
    bool pairing_failed;
    uint32_t revision;
} gamepad_discovery_t;

// Collect existing Xbox/unnamed gamepads and BLE keyboards of any brand.
bool gamepad_discovery_observe(gamepad_discovery_t *state,
                              const uint8_t address[6], uint16_t cod,
                              const char *name);
bool gamepad_discovery_select(gamepad_discovery_t *state, size_t index);
// Start a fresh list only when no pairing/reconnect target is selected.
// Call at explicit scan entry; candidates otherwise keep stable indices.
void gamepad_discovery_begin_scan(gamepad_discovery_t *state);
// Cancellation clears the candidates as well as the selected target and PIN.
void gamepad_discovery_clear_selection(gamepad_discovery_t *state);
bool gamepad_discovery_should_connect(const gamepad_discovery_t *state,
                                      const uint8_t address[6]);
// Ignore security events belonging to a cancelled or different selection.
void gamepad_discovery_passkey(gamepad_discovery_t *state, const uint8_t address[6],
                              uint32_t passkey);
void gamepad_discovery_pairing_done(gamepad_discovery_t *state, const uint8_t address[6],
                                   bool success);
// Truncate a BLE UTF-8 name only at a character boundary.
void gamepad_copy_name(char *out, size_t capacity, const char *name);
