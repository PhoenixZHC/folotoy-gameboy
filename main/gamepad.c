#include "gamepad.h"

#include <string.h>

#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uni.h"
#include "bt/uni_bt.h"

static const char *TAG = "gamepad";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static pad_sample_t s_sample;
static gamepad_discovery_t s_discovery;
static uni_hid_device_t *s_owner;
static bool s_started;
static bool s_scan_allowed = true;
static bool s_scan_update_pending;
static btstack_context_callback_registration_t s_scan_update;

// Apply the latest request on the Bluetooth thread. Rechecking connection
// state here avoids restarting a scan queued just before a controller is ready.
static void apply_scan_policy(void *context) {
    (void)context;
    taskENTER_CRITICAL(&s_lock);
    bool scan = s_scan_allowed && !s_sample.connected;
    s_scan_update_pending = false;
    taskEXIT_CRITICAL(&s_lock);
    if (scan) uni_bt_start_scanning_and_autoconnect_unsafe();
    else uni_bt_stop_scanning_unsafe();
}

static void update_scan_policy(void) {
    taskENTER_CRITICAL(&s_lock);
    bool enqueue = !s_scan_update_pending;
    s_scan_update_pending = true;
    taskEXIT_CRITICAL(&s_lock);
    if (enqueue) {
        s_scan_update.callback = apply_scan_policy;
        btstack_run_loop_execute_on_main_thread(&s_scan_update);
    }
}

static void set_sample(pad_sample_t sample) {
    taskENTER_CRITICAL(&s_lock);
    s_sample = sample;
    taskEXIT_CRITICAL(&s_lock);
}

pad_sample_t gamepad_snapshot(void) {
    taskENTER_CRITICAL(&s_lock);
    pad_sample_t copy = s_sample;
    taskEXIT_CRITICAL(&s_lock);
    return copy;
}

gamepad_discovery_t gamepad_discovery_snapshot(void) {
    taskENTER_CRITICAL(&s_lock);
    gamepad_discovery_t copy = s_discovery;
    taskEXIT_CRITICAL(&s_lock);
    return copy;
}

bool gamepad_select_candidate(size_t index) {
    taskENTER_CRITICAL(&s_lock);
    bool selected = gamepad_discovery_select(&s_discovery, index);
    taskEXIT_CRITICAL(&s_lock);
    return selected;
}

void gamepad_clear_selection(void) {
    taskENTER_CRITICAL(&s_lock);
    gamepad_discovery_clear_selection(&s_discovery);
    taskEXIT_CRITICAL(&s_lock);
}

static void platform_init(int argc, const char **argv) {
    (void)argc;
    (void)argv;
}

static void platform_ready(void) {
    uni_bt_start_scanning_and_autoconnect_unsafe();
    ESP_LOGI(TAG, "BLE scan started; hold the Xbox 1914 pair button for 3 seconds");
}

static uni_error_t device_discovered(bd_addr_t addr, const char *name,
                                     uint16_t cod, uint8_t rssi) {
    (void)addr;
    (void)cod;
    (void)rssi;
    taskENTER_CRITICAL(&s_lock);
    bool candidate = gamepad_discovery_observe(&s_discovery, addr, cod, name);
    bool selected = candidate && gamepad_discovery_should_connect(&s_discovery, addr);
    taskEXIT_CRITICAL(&s_lock);
    if (!selected) return UNI_ERROR_IGNORE_DEVICE;
    ESP_LOGI(TAG, "selected BLE gamepad found (%s)",
             name && name[0] ? name : "unnamed");
    return UNI_ERROR_SUCCESS;
}

static void device_connected(uni_hid_device_t *device) {
    ESP_LOGI(TAG, "controller link: %p", device);
}

static void device_disconnected(uni_hid_device_t *device) {
    if (device != s_owner) return;
    s_owner = NULL;
    set_sample((pad_sample_t){0});
    update_scan_policy();
    ESP_LOGW(TAG, "controller disconnected; all buttons released");
}

static uni_error_t device_ready(uni_hid_device_t *device) {
    if (s_owner && s_owner != device) return UNI_ERROR_IGNORE_DEVICE;
    s_owner = device;
    set_sample((pad_sample_t){.connected = true});
    update_scan_policy();
    ESP_LOGI(TAG, "controller ready");
    return UNI_ERROR_SUCCESS;
}

static void controller_data(uni_hid_device_t *device, uni_controller_t *ctl) {
    if (device != s_owner || ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) return;
    const uni_gamepad_t *gp = &ctl->gamepad;
    pad_sample_t next = {
        .connected = true,
        .right = (gp->dpad & DPAD_RIGHT) != 0,
        .left = (gp->dpad & DPAD_LEFT) != 0,
        .up = (gp->dpad & DPAD_UP) != 0,
        .down = (gp->dpad & DPAD_DOWN) != 0,
        .a = (gp->buttons & BUTTON_A) != 0,
        .b = (gp->buttons & BUTTON_B) != 0,
        .view = (gp->misc_buttons & MISC_BUTTON_SELECT) != 0,
        .menu = (gp->misc_buttons & MISC_BUTTON_START) != 0,
        .any_button = gp->buttons || gp->misc_buttons || gp->dpad ||
                      gp->brake > 64 || gp->throttle > 64,
    };
    set_sample(next);
}

static const uni_property_t *get_property(uni_property_idx_t idx) {
    (void)idx;
    return NULL;
}

static void platform_oob_event(uni_platform_oob_event_t event, void *data) {
    (void)data;
    if (event == UNI_PLATFORM_OOB_BLUETOOTH_ENABLED)
        ESP_LOGI(TAG, "controller scanning state changed");
}

static struct uni_platform s_platform = {
    .name = "folotoy_gameboy",
    .init = platform_init,
    .on_init_complete = platform_ready,
    .on_device_discovered = device_discovered,
    .on_device_connected = device_connected,
    .on_device_disconnected = device_disconnected,
    .on_device_ready = device_ready,
    .on_controller_data = controller_data,
    .get_property = get_property,
    .on_oob_event = platform_oob_event,
};

static void bt_task(void *unused) {
    (void)unused;
    btstack_init();
    uni_platform_set_custom(&s_platform);
    uni_init(0, NULL);
    btstack_run_loop_execute();
    vTaskDelete(NULL);
}

esp_err_t gamepad_start(void) {
    if (s_started) return ESP_OK;
    BaseType_t ok = xTaskCreate(bt_task, "gamepad_bt", 8192, NULL, 5, NULL);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;
    s_started = true;
    return ESP_OK;
}

void gamepad_set_scanning(bool enabled) {
    if (!s_started) return;
    taskENTER_CRITICAL(&s_lock);
    s_scan_allowed = enabled;
    taskEXIT_CRITICAL(&s_lock);
    update_scan_policy();
}
