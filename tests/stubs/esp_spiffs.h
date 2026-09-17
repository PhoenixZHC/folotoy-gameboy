#pragma once
#include <stddef.h>

typedef struct {
    const char *base_path;
    const char *partition_label;
    size_t max_files;
    int format_if_mount_failed;
} esp_vfs_spiffs_conf_t;

#define ESP_OK 0
static inline int esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *config) {
    (void)config;
    return ESP_OK;
}
