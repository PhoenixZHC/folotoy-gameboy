#include "gb_audio.h"
#include "gb_audio_pacing.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

typedef struct {
    bool stop;
    uint16_t output_count;
    int16_t samples[GB_AUDIO_SAMPLES];
} audio_block_t;

// APU 和输出统一为 14 kHz，避免生成额外采样后再逐块重采样。
// 低帧率仍由调用方按墙钟推进 APU，不改变输出采样率。

static const char *TAG = "gb_audio";
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_stopped;
static TaskHandle_t s_task;
static volatile bool s_failed;
static volatile bool s_stop_requested;
static uint32_t s_dropped;
static volatile uint32_t s_output_samples;
static volatile uint32_t s_waits;
static volatile uint32_t s_max_wait_us;
static uint64_t s_submitted_blocks;
static int16_t s_output[GB_AUDIO_MAX_OUTPUT_SAMPLES * 2];

static void resample_block(const int16_t source[GB_AUDIO_SAMPLES],
                           int16_t output[GB_AUDIO_MAX_OUTPUT_SAMPLES], size_t count) {
    if (count == GB_AUDIO_SAMPLES) {
        memcpy(output, source, count * sizeof(*output));
        return;
    }
    uint32_t position = 0;
    const uint32_t step = ((GB_AUDIO_SAMPLES - 1u) << 8) / (count - 1u);
    for (size_t i = 0; i < count; i++) {
        size_t index = position >> 8;
        if (index >= GB_AUDIO_SAMPLES - 1u) output[i] = source[GB_AUDIO_SAMPLES - 1u];
        else {
            int32_t delta = (int32_t)source[index + 1] - source[index];
            output[i] = (int16_t)((int32_t)source[index] +
                                  ((delta * (int32_t)(position & 255u)) >> 8));
        }
        position += step;
    }
    output[count - 1u] = source[GB_AUDIO_SAMPLES - 1u];
}

static void audio_task(void *arg) {
    (void)arg;
    audio_block_t block;
    // 启动时先积攒四帧，让较慢游戏也有足够的音频缓冲。
    while (uxQueueMessagesWaiting(s_queue) < 4 && !s_stop_requested) vTaskDelay(1);
    while (!s_stop_requested) {
        size_t count = 0;
        bool stop = false;
        for (int i = 0; i < 2; i++) {
            int64_t waiting_since = esp_timer_get_time();
            if (xQueueReceive(s_queue, &block, portMAX_DELAY) != pdTRUE || block.stop) {
                stop = true;
                break;
            }
            uint32_t waited = (uint32_t)(esp_timer_get_time() - waiting_since);
            if (waited > 5000) s_waits++;
            if (waited > s_max_wait_us) s_max_wait_us = waited;
            resample_block(block.samples, s_output + count, block.output_count);
            count += block.output_count;
        }
        if (stop) break;
        if (bsp_audio_write(s_output, count * sizeof(s_output[0])) != ESP_OK) {
            ESP_LOGE(TAG, "PCM output failed");
            s_failed = true;
            break;
        }
        s_output_samples += count;
    }
    bsp_audio_set_volume(0);
    xSemaphoreGive(s_stopped);
    vTaskDelete(NULL);
}

bool gb_audio_start(uint8_t volume) {
    if (s_task || s_queue) return false;
    esp_err_t e = bsp_audio_init_playback();
    if (e == ESP_OK) e = bsp_audio_set_format(GB_AUDIO_OUTPUT_RATE, 16, 1);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "codec init failed: %s", esp_err_to_name(e));
        return false;
    }
    s_queue = xQueueCreate(8, sizeof(audio_block_t));
    s_stopped = xSemaphoreCreateBinary();
    if (!s_queue || !s_stopped) {
        if (s_queue) vQueueDelete(s_queue);
        if (s_stopped) vSemaphoreDelete(s_stopped);
        s_queue = NULL;
        s_stopped = NULL;
        return false;
    }
    s_failed = false;
    s_stop_requested = false;
    s_dropped = 0;
    s_output_samples = 0;
    s_waits = 0;
    s_max_wait_us = 0;
    s_submitted_blocks = 0;
    bsp_audio_set_volume(volume);
    if (xTaskCreate(audio_task, "gb_audio", 5120, NULL, 4, &s_task) != pdPASS) {
        bsp_audio_set_volume(0);
        vQueueDelete(s_queue); vSemaphoreDelete(s_stopped);
        s_queue = NULL; s_stopped = NULL;
        return false;
    }
    return true;
}

void gb_audio_submit(const int16_t *samples, size_t count) {
    if (!s_queue || !samples || count != GB_AUDIO_SAMPLES || s_failed) return;
    audio_block_t block = {.stop = false};
    block.output_count = (uint16_t)gb_audio_resampled_count(++s_submitted_blocks);
    memcpy(block.samples, samples, sizeof(block.samples));
    if (xQueueSend(s_queue, &block, 0) != pdTRUE) {
        audio_block_t stale;
        if (xQueueReceive(s_queue, &stale, 0) == pdTRUE) s_dropped++;
        if (xQueueSend(s_queue, &block, 0) != pdTRUE) s_dropped++;
    }
}

bool gb_audio_healthy(void) { return !s_failed; }
uint32_t gb_audio_dropped(void) { return s_dropped; }
uint32_t gb_audio_output_samples(void) { return s_output_samples; }
uint32_t gb_audio_requested_samples(void) {
    return (uint32_t)(s_submitted_blocks * GB_AUDIO_SOURCE_SAMPLES *
                      GB_AUDIO_OUTPUT_RATE / GB_AUDIO_SOURCE_RATE);
}
uint32_t gb_audio_waits(void) { return s_waits; }
uint32_t gb_audio_max_wait_us(void) { return s_max_wait_us; }

bool gb_audio_stop(void) {
    if (!s_queue) return true;
    s_stop_requested = true;
    xQueueReset(s_queue);
    audio_block_t block = {.stop = true};
    xQueueSend(s_queue, &block, 0);
    if (xSemaphoreTake(s_stopped, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "audio task did not stop");
        return false;
    }
    s_task = NULL;
    vQueueDelete(s_queue);
    vSemaphoreDelete(s_stopped);
    s_queue = NULL;
    s_stopped = NULL;
    return !s_failed;
}
