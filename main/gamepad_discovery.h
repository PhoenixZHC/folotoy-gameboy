#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAMEPAD_DISCOVERY_MAX 8

typedef struct {
    uint8_t address[6];
    char name[32];
} gamepad_candidate_t;

typedef struct {
    gamepad_candidate_t candidates[GAMEPAD_DISCOVERY_MAX];
    size_t count;
    bool selected;
    uint8_t selected_address[6];
    uint32_t revision;
} gamepad_discovery_t;

// 仅收集 Xbox 名称或尚无名称的 BLE 游戏手柄广播。
bool gamepad_discovery_observe(gamepad_discovery_t *state,
                              const uint8_t address[6], uint16_t cod,
                              const char *name);
bool gamepad_discovery_select(gamepad_discovery_t *state, size_t index);
void gamepad_discovery_clear_selection(gamepad_discovery_t *state);
bool gamepad_discovery_should_connect(const gamepad_discovery_t *state,
                                      const uint8_t address[6]);
