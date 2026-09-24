#include <stdio.h>
#include <stdlib.h>
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gamepad.h"
#include "gb_display.h"
#include "gb_boot.h"
#include "gb_screen_sleep.h"
#include "gb_audio.h"
#include "gb_audio_pacing.h"
#include "gb_frame_pacing.h"
#include "gb_input.h"
#include "gb_port.h"
#include "gb_saves.h"
#include "gb_storage.h"
#include "gb_web.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "folotoy_gb";
static QueueHandle_t s_buttons;
static bool s_saves_ready;
static uint8_t s_volume = 60;
static uint8_t s_brightness = 80;
static bool s_sound_enabled = true;
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } button_message_t;

static void button_callback(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    button_message_t msg = {button, event};
    if (s_buttons) xQueueSend(s_buttons, &msg, 0);
}

static int battery_percent(void) { return bsp_battery_soc(); }

static void show_ui(const char *title, const char *hint, const char *const *items,
                    size_t count, size_t selected, const char *footer) {
    gb_ui_model_t model = {.title = title, .hint = hint, .count = count,
                           .selected = selected, .footer = footer,
                           .battery_percent = battery_percent()};
    for (size_t i = 0; i < count && i < GB_UI_MAX_ITEMS; i++) model.items[i] = items[i];
    esp_err_t e = gb_display_ui(&model);
    if (e != ESP_OK) ESP_LOGE(TAG, "UI: %s", esp_err_to_name(e));
}

static void startup_error(const char *reason) {
    const char *items[] = {reason};
    gb_ui_model_t model = {.title = "启动失败", .hint = "请重启设备",
                           .count = 1, .selected = 0, .footer = "",
                           .battery_percent = -1};
    model.items[0] = items[0];
    gb_display_ui(&model);
}

static void settings_load(void) {
    nvs_handle_t handle;
    if (nvs_open("gb_settings", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t value;
    if (nvs_get_u8(handle, "volume", &value) == ESP_OK && value <= 100) s_volume = value;
    if (nvs_get_u8(handle, "bright", &value) == ESP_OK && value <= 100) s_brightness = value;
    if (nvs_get_u8(handle, "sound", &value) == ESP_OK && value <= 1) s_sound_enabled = value != 0;
    nvs_close(handle);
}

static bool settings_save(void) {
    nvs_handle_t handle;
    esp_err_t e = nvs_open("gb_settings", NVS_READWRITE, &handle);
    if (e != ESP_OK) return false;
    if (e == ESP_OK) e = nvs_set_u8(handle, "volume", s_volume);
    if (e == ESP_OK) e = nvs_set_u8(handle, "bright", s_brightness);
    if (e == ESP_OK) e = nvs_set_u8(handle, "sound", s_sound_enabled ? 1 : 0);
    if (e == ESP_OK) e = nvs_commit(handle);
    nvs_close(handle);
    if (e != ESP_OK) ESP_LOGE(TAG, "settings save: %s", esp_err_to_name(e));
    return e == ESP_OK;
}

static gb_menu_action_t board_menu_action(button_message_t msg) {
    if (msg.event != BSP_BTN_CLICK) return GB_MENU_NONE;
    if (msg.button == BSP_BTN_UP) return GB_MENU_UP;
    if (msg.button == BSP_BTN_DOWN) return GB_MENU_DOWN;
    if (msg.button == BSP_BTN_OK) return GB_MENU_CONFIRM;
    return GB_MENU_NONE;
}

static void settings_loop(void) {
    size_t selected = 0;
    bool editing = false, dirty = true;
    gb_menu_input_t pad_input;
    gb_menu_input_init(&pad_input, gamepad_snapshot());
    int64_t refreshed = 0;
    int64_t footer_started = esp_timer_get_time(), footer_refreshed = 0;
    while (true) {
        button_message_t msg;
        bool board_event = xQueueReceive(s_buttons, &msg, pdMS_TO_TICKS(50)) == pdTRUE;
        gb_menu_action_t pad_action = gb_menu_input_step(&pad_input, gamepad_snapshot());
        gb_menu_action_t action = board_event ? board_menu_action(msg) : GB_MENU_NONE;
        if (board_event && msg.event == BSP_BTN_LONG && msg.button == BSP_BTN_OK)
            action = GB_MENU_BACK;
        if (action == GB_MENU_NONE) action = pad_action;
        if (action == GB_MENU_BACK) {
            if (editing) { editing = false; settings_save(); }
            else return;
            dirty = true;
        } else if (action != GB_MENU_NONE) {
            if (editing) {
                if (action == GB_MENU_UP || action == GB_MENU_DOWN) {
                    int change = action == GB_MENU_UP ? 10 : -10;
                    if (selected == 0) {
                        int value = (int)s_volume + change;
                        s_volume = value < 0 ? 0 : value > 100 ? 100 : value;
                    } else if (selected == 1) {
                        int value = (int)s_brightness + change;
                        s_brightness = value < 10 ? 10 : value > 100 ? 100 : value;
                        bsp_display_backlight(s_brightness);
                    }
                } else if (action == GB_MENU_CONFIRM) {
                    editing = false;
                    settings_save();
                }
            } else if (action == GB_MENU_UP) selected = (selected + 3) % 4;
            else if (action == GB_MENU_DOWN) selected = (selected + 1) % 4;
            else if (action == GB_MENU_CONFIRM) {
                if (selected == 3) return;
                if (selected == 2) { s_sound_enabled = !s_sound_enabled; settings_save(); }
                else editing = true;
            }
            dirty = true;
        }
        int64_t now = esp_timer_get_time();
        const char *footer = editing ? "上/下调整 OK保存 长按OK结束"
                                     : "上/下选择 OK确认 长按OK返回";
        if (dirty || now - refreshed > 15000000) {
            char vol[32], bright[32], sound[32];
            snprintf(vol, sizeof(vol), "音量 %u%%", s_volume);
            snprintf(bright, sizeof(bright), "亮度 %u%%", s_brightness);
            snprintf(sound, sizeof(sound), "声音 %s", s_sound_enabled ? "开" : "关");
            const char *items[] = {vol, bright, sound, "返回"};
            show_ui("设置", editing ? "正在调整" : "设备选项",
                    items, 4, selected, footer);
            refreshed = now;
            if (dirty) footer_started = now;
            dirty = false;
        }
        if (now - footer_refreshed >= 100000) {
            esp_err_t e = gb_display_footer(footer, (uint32_t)((now - footer_started) / 40000));
            if (e != ESP_OK) ESP_LOGE(TAG, "settings footer: %s", esp_err_to_name(e));
            footer_refreshed = now;
        }
    }
}

static size_t home_loop(void) {
    const char *items[] = {"配对手柄", "游戏管理", "设置"};
    size_t selected = 0;
    bool dirty = true;
    int64_t refreshed = 0;
    while (true) {
        button_message_t msg;
        if (xQueueReceive(s_buttons, &msg, pdMS_TO_TICKS(100)) == pdTRUE &&
            msg.event == BSP_BTN_CLICK) {
            if (msg.button == BSP_BTN_UP) selected = (selected + 2) % 3;
            else if (msg.button == BSP_BTN_DOWN) selected = (selected + 1) % 3;
            else if (msg.button == BSP_BTN_OK) return selected;
            dirty = true;
        }
        int64_t now = esp_timer_get_time();
        if (dirty || now - refreshed > 15000000) {
            show_ui("GB模拟器", gamepad_snapshot().connected ? "手柄已连接" : "请选择操作",
                    items, 3, selected, "上/下 选择 OK 确认");
            refreshed = now;
            dirty = false;
        }
    }
}

static void management_loop(void) {
    gamepad_set_scanning(false);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (s_saves_ready && gb_storage_catalog_trusted() &&
        !gb_saves_prune_orphans()) ESP_LOGE(TAG, "orphan save cleanup failed");
    if (!gb_web_start()) {
        gamepad_set_scanning(true);
        const char *items[] = {"无线网络开启失败", "返回"};
        show_ui("游戏管理", "内存或网络不可用", items, 2, 1,
                "OK 返回");
        button_message_t msg;
        while (xQueueReceive(s_buttons, &msg, portMAX_DELAY) == pdTRUE)
            if (msg.button == BSP_BTN_OK) return;
    }
    gb_web_status_t status = gb_web_status();
    // 清除打开此页时残留的 OK 事件；仅长按才退出。
    xQueueReset(s_buttons);
    int64_t refreshed = 0;
    while (true) {
        button_message_t msg;
        if (xQueueReceive(s_buttons, &msg, pdMS_TO_TICKS(200)) == pdTRUE &&
            msg.button == BSP_BTN_OK && msg.event == BSP_BTN_LONG) {
            ESP_LOGI(TAG, "Leaving game management: OK long press");
            break;
        }
        int64_t now = esp_timer_get_time();
        if (!refreshed || now - refreshed > 2000000) {
            char ssid[38], games[38], space[38];
            snprintf(ssid, sizeof(ssid), "WiFi %s", status.ssid);
            snprintf(games, sizeof(games), "游戏数量 %u", (unsigned)gb_storage_count());
            snprintf(space, sizeof(space), "剩余空间 %uKB",
                     (unsigned)(gb_storage_available() / 1024));
            const char *items[] = {ssid, "地址 192.168.4.1", games, space};
            gb_ui_model_t model = {.title = "游戏管理", .hint = "连接WiFi后打开网页",
                                   .count = 4, .selected = 4, .footer = "长按OK 返回",
                                   .battery_percent = battery_percent(), .compact = true};
            for (size_t i = 0; i < 4; i++) model.items[i] = items[i];
            esp_err_t draw_error = gb_display_ui(&model);
            if (draw_error != ESP_OK)
                ESP_LOGE(TAG, "management UI: %s", esp_err_to_name(draw_error));
            refreshed = now;
        }
    }
    gb_web_stop();
    gamepad_set_scanning(true);
}

static void show_error(const char *reason) {
    ESP_LOGE(TAG, "%s", reason);
    const char *items[] = {reason, "返回"};
    show_ui("发生错误", "请查看提示", items, 2, 1, "OK 返回");
    button_message_t msg;
    do { xQueueReceive(s_buttons, &msg, portMAX_DELAY); }
    while (msg.button != BSP_BTN_OK || msg.event != BSP_BTN_CLICK);
}

static bool screen_off_loop(void) {
    if (!gb_audio_stop()) return false;
    esp_err_t e = bsp_audio_sleep();
    if (e != ESP_OK) { bsp_audio_wake(); return false; }
    e = gb_display_screen_on(false);
    if (e != ESP_OK) { bsp_audio_wake(); return false; }
    bsp_display_backlight(0);
    ESP_LOGI(TAG, "screen off: emulator paused, audio suspended");
    gb_screen_sleep_t state = {0};
    bool lit = false;
    while (state.phase != 3) {
        int mv = bsp_button_read_mv();
        pad_sample_t pad = gamepad_snapshot();
        bool pressed = (mv >= 0 && mv < 1900) || (pad.connected && pad.any_button);
        gb_screen_sleep_step(&state, pressed, (uint32_t)(esp_timer_get_time() / 1000));
        button_message_t ignored;
        while (xQueueReceive(s_buttons, &ignored, 0) == pdTRUE) { }
        if (state.phase >= 2 && !lit) {
            e = gb_display_screen_on(true);
            bsp_display_backlight(s_brightness);
            lit = true;
            if (e != ESP_OK) break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    esp_err_t audio = bsp_audio_wake();
    ESP_LOGI(TAG, "screen wake: display=%s audio=%s", esp_err_to_name(e), esp_err_to_name(audio));
    return e == ESP_OK && audio == ESP_OK;
}

static bool pause_game(gb_session_t *session, const uint8_t hash[32], bool connected) {
    bool battery = gb_port_has_battery(session);
    const char *items[] = {"继续游戏", battery ? "同步电池存档" : "无电池存档",
                           "设置", "息屏", "退出游戏"};
    const char *hint = battery ? "游戏内存档，重进后读取" : "此游戏无电池存档";
    if (!gb_saves_write(session, hash)) hint = "同步失败，进度仅在内存";
    size_t selected = 0;
    show_ui(connected ? "游戏暂停" : "手柄断开", hint, items, 5, selected,
            "上/下选择 OK确认");
    button_message_t msg;
    gb_menu_input_t pad_input;
    gb_menu_input_init(&pad_input, gamepad_snapshot());
    while (true) {
        bool board_event = xQueueReceive(s_buttons, &msg, pdMS_TO_TICKS(50)) == pdTRUE;
        pad_sample_t pad = gamepad_snapshot();
        gb_menu_action_t pad_action = gb_menu_input_step(&pad_input, pad);
        gb_menu_action_t action = board_event ? board_menu_action(msg) : GB_MENU_NONE;
        bool redraw = board_event;
        if (board_event && msg.button == BSP_BTN_OK && msg.event == BSP_BTN_LONG) {
            if (gb_saves_write(session, hash)) return false;
            hint = "同步失败，进度仅在内存";
        } else {
            if (action == GB_MENU_NONE) action = pad_action;
            if (action != GB_MENU_NONE) redraw = true;
            if (action == GB_MENU_UP) selected = (selected + 4) % 5;
            else if (action == GB_MENU_DOWN) selected = (selected + 1) % 5;
            else if (action == GB_MENU_BACK) {
                if (pad.connected) return true;
                hint = "请重新连接手柄";
            } else if (action == GB_MENU_CONFIRM) {
                if (selected == 0) {
                    if (pad.connected) return true;
                    hint = "请重新连接手柄";
                } else if (selected == 1) {
                    if (!battery) hint = "此游戏无电池存档";
                    else if (!gb_port_needs_save(session)) hint = "没有新存档";
                    else hint = gb_saves_write(session, hash)
                                    ? "同步完成，重进后读取" : "同步失败，进度仅在内存";
                } else if (selected == 2) {
                    settings_loop();
                    gb_menu_input_init(&pad_input, gamepad_snapshot());
                    hint = battery ? "游戏内存档，重进后读取" : "此游戏无电池存档";
                } else if (selected == 3) {
                    if (!screen_off_loop()) hint = "息屏恢复失败，请重试";
                    else if (gamepad_snapshot().connected) return true;
                    else hint = "请重新连接手柄";
                    gb_menu_input_init(&pad_input, gamepad_snapshot());
                } else {
                    if (gb_saves_write(session, hash)) return false;
                    hint = "同步失败，进度仅在内存";
                }
            }
        }
        bool now_connected = gamepad_snapshot().connected;
        redraw |= now_connected != connected;
        connected = now_connected;
        if (!redraw) continue;
        show_ui(connected ? "游戏暂停" : "手柄断开",
                selected == 3 ? "任意按键唤醒" : hint, items, 5, selected,
                "上/下选择 OK确认");
    }
}

static gb_port_keys_t port_keys(gb_keys_t k) {
    return (gb_port_keys_t){.right=k.right,.left=k.left,.up=k.up,.down=k.down,
                            .a=k.a,.b=k.b,.select=k.select,.start=k.start};
}

typedef struct {
    bool pause;
    bool start;
    bool select;
} game_button_actions_t;

static game_button_actions_t poll_game_buttons(void) {
    button_message_t msg;
    game_button_actions_t actions = {0};
    while (xQueueReceive(s_buttons, &msg, 0) == pdTRUE) {
        if (msg.button == BSP_BTN_OK && msg.event == BSP_BTN_LONG)
            actions.pause = true;
        else if (msg.event == BSP_BTN_CLICK) {
            if (msg.button == BSP_BTN_OK) actions.start = true;
            else if (msg.button == BSP_BTN_UP) actions.select = true;
        }
    }
    return actions;
}

static bool pairing_loop(void) {
    size_t selected = 0;
    uint32_t shown_revision = UINT32_MAX;
    bool dirty = true;
    int64_t selected_at_us = 0;
    bool connection_slow = false;
    while (!gamepad_snapshot().connected) {
        gamepad_discovery_t discovery = gamepad_discovery_snapshot();
        if (discovery.count && selected >= discovery.count) selected = discovery.count - 1;
        button_message_t msg;
        while (xQueueReceive(s_buttons, &msg, 0) == pdTRUE) {
            if (msg.button == BSP_BTN_OK && msg.event == BSP_BTN_LONG) {
                if (!discovery.selected) return false;
                gamepad_clear_selection();
                selected_at_us = 0;
                connection_slow = false;
                dirty = true;
            } else if (!discovery.selected && msg.event == BSP_BTN_CLICK && discovery.count) {
                if (msg.button == BSP_BTN_UP)
                    selected = (selected + discovery.count - 1) % discovery.count;
                else if (msg.button == BSP_BTN_DOWN)
                    selected = (selected + 1) % discovery.count;
                else if (msg.button == BSP_BTN_OK && gamepad_select_candidate(selected)) {
                    selected_at_us = esp_timer_get_time();
                    connection_slow = false;
                }
                dirty = true;
            }
        }
        discovery = gamepad_discovery_snapshot();
        if (discovery.selected && selected_at_us == 0) selected_at_us = esp_timer_get_time();
        if (discovery.selected && !connection_slow &&
            esp_timer_get_time() - selected_at_us >= 25000000) {
            connection_slow = true;
            dirty = true;
        }
        if (discovery.revision != shown_revision) dirty = true;
        if (dirty) {
            const char *items[GB_UI_MAX_ITEMS] = {0};
            char choices[GB_UI_MAX_ITEMS][38] = {{0}};
            size_t count = 0, selected_item = 0;
            const char *hint = "请按手柄配对键";
            if (discovery.selected) {
                items[0] = connection_slow ? "连接失败 请重试" : "连接中...";
                count = 1;
                hint = "长按OK 重试";
            } else if (!discovery.count) {
                items[0] = "扫描中...";
                count = 1;
            } else {
                size_t first = selected > 3 ? selected - 3 : 0;
                selected_item = selected - first;
                for (size_t i = first; i < discovery.count && count < GB_UI_MAX_ITEMS; i++) {
                    const gamepad_candidate_t *candidate = &discovery.candidates[i];
                    snprintf(choices[count], sizeof(choices[count]), "%.18s %02X:%02X",
                             candidate->name[0] ? candidate->name : "手柄",
                             candidate->address[4], candidate->address[5]);
                    items[count] = choices[count];
                    count++;
                }
            }
            show_ui("配对手柄", hint, items, count, selected_item,
                    "上/下 选择 OK 连接");
            shown_revision = discovery.revision;
            dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return true;
}

static bool prepare_game_screen(void) {
    esp_err_t e = gb_display_clear(GB_LCD_LIGHT);
    if (e == ESP_OK) e = gb_display_game_hint("长按OK 暂停或退出");
    if (e != ESP_OK) ESP_LOGE(TAG, "game screen: %s", esp_err_to_name(e));
    return e == ESP_OK;
}

static void run_game(size_t index) {
    const gb_rom_entry_t *entry = gb_storage_entry(index);
    if (!entry) return;
    const char *loading[] = {entry->name};
    show_ui("加载游戏", "正在检查游戏", loading, 1, 0, "请稍候");
    gb_rom_source_t rom;
    if (!gb_storage_open(index, &rom)) { show_error("游戏文件校验失败"); return; }
    gb_session_t *session = NULL;
    if (!gb_port_create(&rom, &session)) { show_error("游戏不支持/内存不足"); return; }
    ESP_LOGI(TAG, "ROM loaded: %.31s", entry->name);
    if (gb_port_has_battery(session)) {
        if (!s_saves_ready) {
            gb_port_destroy(session); show_error("存档空间不可用"); return;
        }
        if (gb_saves_load(session, entry->sha256) == GB_SAVE_ERROR) {
            gb_port_destroy(session); show_error("存档副本损坏"); return;
        }
    }
    if (!gamepad_snapshot().connected && !pause_game(session, entry->sha256, false)) {
        gb_port_destroy(session); return;
    }
    bool audio_enabled = s_sound_enabled && s_volume > 0;
    bool color_game = gb_port_is_cgb(session);
    if (audio_enabled && !gb_audio_start(s_volume)) {
        gb_port_destroy(session); show_error("声音启动失败"); return;
    }
    if (!prepare_game_screen()) {
        if (audio_enabled) gb_audio_stop();
        gb_port_destroy(session); show_error("屏幕传输失败"); return;
    }
    int64_t next = esp_timer_get_time();
    int64_t last_save = next;
    int64_t last_report = next;
    int64_t last_idle_yield = next;
    uint32_t report_idle_yields = 0;
    uint32_t frames = 0;
    uint32_t report_frames = 0;
    uint32_t dropped_frames = 0;
    uint64_t report_core_us = 0, report_audio_us = 0;
    uint64_t report_draw_us = 0, report_skip_us = 0;
    uint32_t report_draw_frames = 0, report_skip_frames = 0;
    uint32_t report_key_changes = 0, report_multi_keys = 0;
    uint8_t previous_keys = 0;
    uint64_t audio_blocks = 0;
    int64_t audio_epoch = next;
    gb_frame_pacing_t pacing;
    gb_frame_pacing_init(&pacing);
    while (true) {
        game_button_actions_t board = poll_game_buttons();
        pad_sample_t pad = gamepad_snapshot();
        if (board.pause || !pad.connected) {
            gb_port_set_keys(session, (gb_port_keys_t){0});
            if (audio_enabled && !gb_audio_stop()) {
                gb_port_destroy(session); show_error("声音停止失败"); return;
            }
            if (!pause_game(session, entry->sha256, pad.connected)) break;
            audio_enabled = s_sound_enabled && s_volume > 0;
            if (audio_enabled && !gb_audio_start(s_volume)) {
                gb_port_destroy(session); show_error("声音启动失败"); return;
            }
            if (!prepare_game_screen()) {
                if (audio_enabled) gb_audio_stop();
                gb_port_destroy(session); show_error("屏幕传输失败"); return;
            }
            next = esp_timer_get_time();
            last_report = next;
            last_idle_yield = next;
            report_idle_yields = 0;
            report_frames = 0;
            dropped_frames = 0;
            report_core_us = report_audio_us = 0;
            report_draw_us = report_skip_us = 0;
            report_draw_frames = report_skip_frames = 0;
            report_key_changes = report_multi_keys = 0;
            previous_keys = 0;
            audio_epoch = next;
            audio_blocks = 0;
            continue;
        }
        gb_keys_t keys = gb_input_map(pad);
        keys.start |= board.start;
        keys.select |= board.select;
        gb_port_set_keys(session, port_keys(keys));
        uint8_t key_bits = (uint8_t)((pad.right ? 1 : 0) | (pad.left ? 2 : 0) |
                           (pad.up ? 4 : 0) | (pad.down ? 8 : 0) |
                           (pad.a ? 16 : 0) | (pad.b ? 32 : 0) |
                           (pad.view ? 64 : 0) | (pad.menu ? 128 : 0));
        if (key_bits != previous_keys) report_key_changes++;
        if (__builtin_popcount((unsigned)key_bits) >= 2) report_multi_keys++;
        previous_keys = key_bits;
        bool draw = gb_frame_pacing_draw(&pacing);
        int64_t frame_started = esp_timer_get_time();
        if (!gb_port_step_frame(session, draw)) {
            if (audio_enabled) gb_audio_stop();
            gb_port_destroy(session); show_error("游戏读取失败"); return;
        }
        int64_t core_done = esp_timer_get_time();
        report_core_us += (uint64_t)(core_done - frame_started);
        if (draw) { report_draw_us += (uint64_t)(core_done - frame_started); report_draw_frames++; }
        else { report_skip_us += (uint64_t)(core_done - frame_started); report_skip_frames++; }
        if (audio_enabled) {
            int16_t samples[GB_AUDIO_SAMPLES];
            size_t due = gb_audio_blocks_due(
                (uint64_t)(esp_timer_get_time() - audio_epoch), audio_blocks);
            for (size_t i = 0; i < due; i++) {
                size_t produced = gb_port_audio_frame(session, samples, GB_AUDIO_SAMPLES);
                if (produced != GB_AUDIO_SAMPLES || !gb_audio_healthy()) {
                    gb_audio_stop();
                    gb_port_destroy(session); show_error("声音输出失败"); return;
                }
                gb_audio_submit(samples, produced);
                audio_blocks++;
            }
        }
        int64_t audio_done = esp_timer_get_time();
        report_audio_us += (uint64_t)(audio_done - core_done);
        if (draw) {
            bool queued = false;
            esp_err_t display_error;
            if (color_game) {
                display_error = gb_display_color_frame(gb_port_color_framebuffer(session),
                                   gb_port_bg_palette(session), gb_port_obj_palette(session));
                queued = true;
            } else display_error = gb_display_submit_frame(gb_port_framebuffer(session),
                                                            true, &queued);
            if (display_error != ESP_OK) {
                if (audio_enabled) gb_audio_stop();
                gb_port_destroy(session); show_error("屏幕传输失败"); return;
            }
            if (!queued) dropped_frames++;
        }
        gb_frame_pacing_record(&pacing, (uint32_t)(esp_timer_get_time() - frame_started));
        frames++;
        report_frames++;
        int64_t now = esp_timer_get_time();
        if (now - last_report >= 10000000LL) {
            ESP_LOGI(TAG, "emu_fps=%.1f draw_every=%u core_us=%lu draw_us=%lu skip_us=%lu audio_us=%lu key_changes=%lu multi_frames=%lu display_drops=%lu audio_drops=%lu audio_waits=%lu max_audio_wait_us=%lu free_heap=%lu min_heap=%lu largest_block=%lu idle_yields=%lu",
                     (double)report_frames * 1000000.0 / (double)(now - last_report),
                     (unsigned)pacing.draw_interval,
                     (unsigned long)(report_core_us / report_frames),
                     (unsigned long)(report_draw_us / (report_draw_frames ? report_draw_frames : 1)),
                     (unsigned long)(report_skip_us / (report_skip_frames ? report_skip_frames : 1)),
                     (unsigned long)(report_audio_us / report_frames),
                     (unsigned long)report_key_changes,
                     (unsigned long)report_multi_keys,
                     (unsigned long)dropped_frames,
                     (unsigned long)gb_audio_dropped(),
                     (unsigned long)gb_audio_waits(),
                     (unsigned long)gb_audio_max_wait_us(),
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)esp_get_minimum_free_heap_size(),
                     (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                     (unsigned long)report_idle_yields);
            last_report = now;
            report_frames = 0;
            dropped_frames = 0;
            report_core_us = report_audio_us = 0;
            report_draw_us = report_skip_us = 0;
            report_draw_frames = report_skip_frames = 0;
            report_key_changes = report_multi_keys = 0;
            report_idle_yields = 0;
        }
        if (now - last_save >= 30000000LL && gb_port_has_battery(session)) {
            if (!gb_saves_write(session, entry->sha256)) {
                ESP_LOGE(TAG, "automatic save failed; pausing with session in RAM");
                if (audio_enabled) gb_audio_stop();
                if (!pause_game(session, entry->sha256, true)) break;
                audio_enabled = s_sound_enabled && s_volume > 0;
                if (audio_enabled && !gb_audio_start(s_volume)) {
                    gb_port_destroy(session); show_error("声音启动失败"); return;
                }
                if (!prepare_game_screen()) {
                    if (audio_enabled) gb_audio_stop();
                    gb_port_destroy(session); show_error("屏幕传输失败"); return;
                }
                next = esp_timer_get_time();
                audio_epoch = next;
                audio_blocks = 0;
                last_report = next;
                report_frames = 0;
                dropped_frames = 0;
                report_core_us = report_audio_us = 0;
                report_draw_us = report_skip_us = 0;
                report_draw_frames = report_skip_frames = 0;
                report_key_changes = report_multi_keys = 0;
            }
            last_save = esp_timer_get_time();
        }
        next += 16743;
        now = esp_timer_get_time();
        // taskYIELD cannot schedule the lower-priority IDLE task. Even when
        // every frame is late, periodically block so IDLE can run its watchdog.
        if (now - last_idle_yield >= 250000) {
            vTaskDelay(1);
            now = esp_timer_get_time();
            last_idle_yield = now;
            report_idle_yields++;
        }
        if (next < now - 16743) next = now;
        while (next - now > (int64_t)portTICK_PERIOD_MS * 1000 + 1500) {
            vTaskDelay(1);
            now = esp_timer_get_time();
            last_idle_yield = now;
            report_idle_yields++;
        }
        while (next > now) {
            taskYIELD();
            now = esp_timer_get_time();
        }
    }
    if (audio_enabled && !gb_audio_stop()) ESP_LOGE(TAG, "audio stop failed");
    gb_port_destroy(session);
}

static void menu_loop(void) {
    size_t selected = 0;
    bool dirty = true, prev_up = false, prev_down = false, prev_a = false;
    while (true) {
        size_t count = gb_storage_count();
        if (!count) {
            const char *items[] = {"没有游戏"};
            show_ui("游戏列表", "请到游戏管理上传", items, 1, 0, "长按OK 返回");
            button_message_t msg;
            while (xQueueReceive(s_buttons, &msg, portMAX_DELAY) == pdTRUE)
                if (msg.button == BSP_BTN_OK && msg.event == BSP_BTN_LONG) return;
        }
        pad_sample_t pad = gamepad_snapshot();
        if (!pad.connected) return;
        button_message_t msg;
        while (xQueueReceive(s_buttons, &msg, 0) == pdTRUE) {
            if (msg.button == BSP_BTN_OK && msg.event == BSP_BTN_LONG) return;
            if (msg.event != BSP_BTN_CLICK) continue;
            if (msg.button == BSP_BTN_UP) { selected = (selected + count - 1) % count; dirty = true; }
            if (msg.button == BSP_BTN_DOWN) { selected = (selected + 1) % count; dirty = true; }
            if (msg.button == BSP_BTN_OK) { run_game(selected); dirty = true; }
        }
        if (pad.up && !prev_up) { selected = (selected + count - 1) % count; dirty = true; }
        if (pad.down && !prev_down) { selected = (selected + 1) % count; dirty = true; }
        if (pad.a && !prev_a) { run_game(selected); dirty = true; }
        prev_up = pad.up; prev_down = pad.down; prev_a = pad.a;
        if (dirty) {
            const char *items[GB_UI_MAX_ITEMS] = {0};
            char names[GB_UI_MAX_ITEMS][38] = {{0}};
            size_t first = selected > 3 ? selected - 3 : 0;
            for (size_t i = 0; i < GB_UI_MAX_ITEMS && first + i < count; i++) {
                const gb_rom_entry_t *entry = gb_storage_entry(first + i);
                snprintf(names[i], sizeof(names[i]), "%.31s", entry->name);
                items[i] = names[i];
            }
            size_t shown = count - first;
            if (shown > GB_UI_MAX_ITEMS) shown = GB_UI_MAX_ITEMS;
            show_ui("游戏列表", "请选择游戏", items, shown, selected - first,
                    "上/下 OK开始 长按OK返回");
            dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool boot_animation(void) {
    uint8_t *frame = malloc(GB_FRAME_BYTES);
    if (!frame) return false;
    esp_err_t e = gb_display_clear(GB_LCD_LIGHT);
    int64_t next = esp_timer_get_time();
    for (int top = -36; top <= 64 && e == ESP_OK; top++) {
        gb_boot_frame(frame, top);
        e = gb_display_boot_frame(frame);
        if (e != ESP_OK) break;
        next += 33486;
        int64_t remaining = next - esp_timer_get_time();
        if (remaining > 1000) vTaskDelay(pdMS_TO_TICKS(remaining / 1000));
        else vTaskDelay(1);
    }
    free(frame);
    if (e != ESP_OK) return false;
    if (s_sound_enabled && s_volume) {
        e = bsp_audio_init_playback();
        if (e == ESP_OK) e = bsp_audio_set_format(14000, 16, 1);
        if (e == ESP_OK) {
            bsp_audio_set_volume(s_volume);
            int16_t pcm[234];
            // Finish with silence so the complete chime clears the I2S DMA queue.
            for (uint32_t at = 0; at < 14000; at += 234) {
                for (unsigned i = 0; i < 234; i++) pcm[i] = gb_boot_sample(at + i);
                e = bsp_audio_write(pcm, sizeof(pcm));
                if (e != ESP_OK) break;
                vTaskDelay(1);
            }
        }
        bsp_audio_set_volume(0);
        if (e != ESP_OK) ESP_LOGE(TAG, "boot chime failed: %s", esp_err_to_name(e));
    } else vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "DMG boot animation complete");
    return e == ESP_OK;
}

void app_main(void) {
    esp_err_t e = nvs_flash_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "NVS: %s", esp_err_to_name(e)); return; }
    e = gb_display_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "display: %s", esp_err_to_name(e)); return; }
    s_buttons = xQueueCreate(16, sizeof(button_message_t));
    if (!s_buttons) { startup_error("按键队列内存不足"); return; }
    e = bsp_button_init(button_callback, NULL);
    if (e != ESP_OK) { startup_error("按键初始化失败"); return; }
    settings_load();
    bsp_display_backlight(s_brightness);
    if (!boot_animation()) { startup_error("开机动画失败"); return; }
    xQueueReset(s_buttons);
    e = gamepad_start();
    if (e != ESP_OK) { startup_error("蓝牙初始化失败"); return; }
    s_saves_ready = gb_saves_init();
    if (bsp_battery_init() != ESP_OK) ESP_LOGW(TAG, "battery unavailable");
    if (!s_saves_ready) ESP_LOGE(TAG, "save partition unavailable; battery games disabled");
    if (!gb_storage_init()) ESP_LOGW(TAG, "ROM container unavailable or invalid");
    else if (s_saves_ready && gb_storage_catalog_trusted() &&
             !gb_saves_prune_orphans()) ESP_LOGE(TAG, "orphan save cleanup failed");
    while (true) {
        size_t choice = home_loop();
        if (choice == 0 && pairing_loop()) menu_loop();
        else if (choice == 1) management_loop();
        else if (choice == 2) settings_loop();
    }
}
