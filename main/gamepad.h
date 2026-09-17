#pragma once

#include "esp_err.h"
#include "gamepad_discovery.h"
#include "gb_input.h"

// 在独立任务中启动 BTstack。调用者周期性读取输入快照。
esp_err_t gamepad_start(void);
void gamepad_set_scanning(bool enabled);
pad_sample_t gamepad_snapshot(void);
gamepad_discovery_t gamepad_discovery_snapshot(void);
bool gamepad_select_candidate(size_t index);
void gamepad_clear_selection(void);
