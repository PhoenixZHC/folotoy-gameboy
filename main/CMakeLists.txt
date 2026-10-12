if(GB_SIM_TEST)
    set(GB_ENTRY "../tests/gb_sim_main.c")
else()
    set(GB_ENTRY "game_main.c")
endif()
idf_component_register(
    SRCS ${GB_ENTRY} "gamepad.c" "gamepad_discovery.c" "gb_input.c"
         "gb_display.c" "gb_pixels.c" "gb_storage.c" "gb_saves.c"
         "ui_font.c" "gb_name.c" "gb_web.c" "gb_audio.c" "gamepad_test.c"
         "gb_audio_pacing.c" "gb_frame_pacing.c" "gb_boot.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "gb_manager.html"
    REQUIRES bsp bluepad32 btstack gb_core nvs_flash esp_lcd esp_timer esp_app_format
             esp_partition mbedtls spiffs esp_wifi esp_netif esp_event esp_http_server
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
