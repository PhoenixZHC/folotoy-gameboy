idf_component_register(
    SRCS "game_main.c" "gamepad.c" "gamepad_discovery.c" "gb_input.c"
         "gb_display.c" "gb_pixels.c" "gb_storage.c" "gb_saves.c"
         "ui_font.c" "gb_name.c" "gb_web.c" "gb_audio.c"
         "gb_audio_pacing.c" "gb_frame_pacing.c" "gb_boot.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "gb_manager.html"
    REQUIRES bsp bluepad32 btstack gb_core nvs_flash esp_lcd esp_timer
             esp_partition mbedtls spiffs esp_wifi esp_netif esp_event esp_http_server
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
