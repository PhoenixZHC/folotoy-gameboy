// Opt-in timing probe. It never writes ROM or saves and is excluded from release builds.
#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_audio.h"
#include "gb_audio.h"
#include "gb_display.h"
#include "gb_port.h"
#include "gb_storage.h"

void gb_audio_probe(void) {
    const char *tag = "audio_probe";
    size_t selected = 0;
    for (size_t i = 1; i < gb_storage_count(); i++) {
        if (gb_storage_entry(i)->size > gb_storage_entry(selected)->size) selected = i;
    }
    if (!gb_storage_count()) { ESP_LOGE(tag, "no ROM"); return; }
    ESP_LOGI(tag, "ROM=%s", gb_storage_entry(selected)->name);
    if (bsp_audio_init_playback() != ESP_OK ||
        bsp_audio_set_format(16000, 16, 1) != ESP_OK) return;
    bsp_audio_set_volume(0);
    static int16_t silence[1000];
    int64_t write_start = esp_timer_get_time();
    for (int i = 0; i < 160; i++) {
        if (bsp_audio_write(silence, sizeof(silence)) != ESP_OK) return;
    }
    ESP_LOGI(tag, "direct_i2s_samples=160000 elapsed_us=%lld",
             (long long)(esp_timer_get_time() - write_start));
    for (int with_audio = 0; with_audio <= 1; with_audio++) {
        gb_rom_source_t rom;
        gb_session_t *session = NULL;
        if (!gb_storage_open(selected, &rom) || !gb_port_create(&rom, &session)) {
            ESP_LOGE(tag, "ROM open/create failed"); return;
        }
        int64_t total_start = esp_timer_get_time();
        int64_t core_us = 0, apu_us = 0;
        int completed = 0;
        for (int frame = 0; frame < 300; frame++) {
            int64_t start = esp_timer_get_time();
            if (!gb_port_step_frame(session, false)) break;
            core_us += esp_timer_get_time() - start;
            if (with_audio) {
                int16_t samples[GB_AUDIO_SAMPLES];
                start = esp_timer_get_time();
                if (gb_port_audio_frame(session, samples, GB_AUDIO_SAMPLES) != GB_AUDIO_SAMPLES) break;
                apu_us += esp_timer_get_time() - start;
            }
            completed++;
        }
        int64_t total_us = esp_timer_get_time() - total_start;
        ESP_LOGI(tag, "audio=%d frames=%d core_us=%lld apu_us=%lld total_us=%lld",
                 with_audio, completed, (long long)core_us, (long long)apu_us,
                 (long long)total_us);
        gb_port_destroy(session);
    }
    for (int with_audio = 0; with_audio <= 1; with_audio++) {
        gb_rom_source_t rom;
        gb_session_t *session = NULL;
        if (!gb_storage_open(selected, &rom) || !gb_port_create(&rom, &session)) {
            ESP_LOGE(tag, "paced ROM open/create failed"); return;
        }
        if (with_audio && !gb_audio_start(30)) {
            ESP_LOGE(tag, "audio worker failed"); gb_port_destroy(session); return;
        }
        if (gb_display_clear(0) != ESP_OK) {
            ESP_LOGE(tag, "display clear failed"); gb_port_destroy(session); return;
        }
        int64_t start = esp_timer_get_time(), next = start;
        int completed = 0, display_drops = 0;
        for (int frame = 0; frame < (with_audio ? 3600 : 600); frame++) {
            bool draw = (frame & 1) == 0;
            if (!gb_port_step_frame(session, draw)) break;
            if (with_audio) {
                int16_t samples[GB_AUDIO_SAMPLES];
                if (gb_port_audio_frame(session, samples, GB_AUDIO_SAMPLES) != GB_AUDIO_SAMPLES) break;
                gb_audio_submit(samples, GB_AUDIO_SAMPLES);
            }
            if (draw) {
                bool queued = false;
                if (gb_display_submit_frame(gb_port_framebuffer(session), true, &queued) != ESP_OK) break;
                if (!queued) display_drops++;
            }
            completed++;
            next += 16743;
            int64_t now = esp_timer_get_time();
            if (next < now - 16743) next = now;
            while (next - now > (int64_t)portTICK_PERIOD_MS * 1000 + 1500) {
                vTaskDelay(1);
                now = esp_timer_get_time();
            }
            while (next > now) { taskYIELD(); now = esp_timer_get_time(); }
        }
        int64_t elapsed = esp_timer_get_time() - start;
        ESP_LOGI(tag, "paced audio=%d frames=%d total_us=%lld display_drops=%d audio_drops=%lu requested=%lu output_samples=%lu",
                 with_audio, completed, (long long)elapsed, display_drops,
                 (unsigned long)gb_audio_dropped(),
                 (unsigned long)gb_audio_requested_samples(),
                 (unsigned long)gb_audio_output_samples());
        if (with_audio) gb_audio_stop();
        gb_port_destroy(session);
    }
}
