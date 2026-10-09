// Isolated PassportSim entry. Never linked into production unless GB_SIM_TEST=ON.
#include <string.h>
#include "gb_port.h"
#include "gb_storage.h"
#include "gb_display.h"
#include "gb_audio.h"
#include "gb_frame_pacing.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
static QueueHandle_t buttons;
static uint8_t saved_frame[5760];
static void on_button(bsp_btn_t button,bsp_btn_ev_t event,void *context) {
    (void)context;
    if(event==BSP_BTN_PRESS) xQueueSend(buttons,&button,0);
}
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(gb_display_init());
    const gb_ui_model_t theme={.title="Game Boy",.hint="SIM TEST",.count=1,
        .items={"Game Boy"},.footer="SIM TEST",.battery_percent=80};
    ESP_ERROR_CHECK(gb_display_ui(&theme));
    ESP_LOGI("gb_sim","GB_SIM_THEME_READY");
    vTaskDelay(pdMS_TO_TICKS(200));
    bsp_display_backlight(75);
    buttons=xQueueCreate(8,sizeof(bsp_btn_t));configASSERT(buttons);
    ESP_ERROR_CHECK(bsp_button_init(on_button,NULL));
    configASSERT(gb_storage_init());
    configASSERT(gb_storage_count()>0);
    // This fixture needs a second Start to confirm its options screen.
    const bool gradius=strcmp(gb_storage_entry(0)->name,"Gradius")==0;
    gb_rom_source_t source;configASSERT(gb_storage_open(0,&source));
    gb_session_t *session=NULL;configASSERT(gb_port_create(&source,&session));
    for(unsigned round=0;round<3;round++) {
        configASSERT(gb_port_step_frame(session,true));
        memcpy(saved_frame,gb_port_framebuffer(session),sizeof(saved_frame));
        bool queued=false;
        ESP_ERROR_CHECK(gb_display_submit_frame(gb_port_framebuffer(session),true,&queued));
        configASSERT(queued);
        for(unsigned skip=0;skip<8;skip++) configASSERT(gb_port_step_frame(session,false));
        configASSERT(!memcmp(saved_frame,gb_port_framebuffer(session),sizeof(saved_frame)));
        ESP_ERROR_CHECK(gb_display_ui(&theme));
        gb_port_destroy(session);session=NULL;
        configASSERT(gb_port_create(&source,&session));
    }
    ESP_LOGI("gb_sim","GB_SIM_LIFECYCLE_OK");
    configASSERT(gb_audio_start(45));
    gb_frame_pacing_t pacing;gb_frame_pacing_init(&pacing);
    int64_t deadline=esp_timer_get_time();
    uint64_t work_total=0;unsigned frames=0;
    ESP_LOGW("gb_sim","SIM_TEST_ONLY: BLE and real hardware performance excluded");
    for(;;) {
        bsp_btn_t button;
        while(xQueueReceive(buttons,&button,0)==pdTRUE)
            ESP_LOGI("gb_sim","GB_SIM_BUTTON=%d",(int)button);
        gb_port_set_keys(session,(gb_port_keys_t){
            .start=(frames>=900 && frames<912) ||
                   (gradius && frames>=1080 && frames<1092),
            .right=frames>=1200,.a=frames>=1200 && frames%40<10});
        bool idle=false;ESP_ERROR_CHECK(gb_display_idle(&idle));
        bool draw=idle && gb_frame_pacing_draw(&pacing);
        int64_t start=esp_timer_get_time();
        configASSERT(gb_port_step_frame(session,draw));
        int16_t samples[GB_AUDIO_SAMPLES];
        size_t count=gb_port_audio_frame(session,samples,GB_AUDIO_SAMPLES);
        configASSERT(count && gb_audio_healthy());gb_audio_submit(samples,count);
        if(draw) {
            bool queued=false;ESP_ERROR_CHECK(gb_display_submit_frame(gb_port_framebuffer(session),true,&queued));
            if(queued) gb_frame_pacing_mark_drawn(&pacing);
        }
        uint32_t work=esp_timer_get_time()-start;work_total+=work;
        gb_frame_pacing_record(&pacing,work);
        if(++frames%120==0) {
            ESP_LOGI("gb_sim","GB_SIM_FRAMES=%u work_us=%llu audio=%lu display=%lu",frames,
                (unsigned long long)work_total,(unsigned long)gb_audio_output_samples(),(unsigned long)gb_display_completed());
        }
        deadline+=16743;
        int64_t wait=deadline-esp_timer_get_time();
        if(wait<0) deadline=esp_timer_get_time();
        vTaskDelay(wait>1000?pdMS_TO_TICKS(wait/1000)+1:1);
    }
}
