#include "gb_web.h"
#include "gb_name.h"
#include "gb_saves.h"
#include "gb_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "gb_web";
static gb_web_status_t s_status;
static esp_netif_t *s_ap;
static httpd_handle_t s_server;
static bool s_wifi_initialized;
static bool s_wifi_started;
static esp_event_handler_instance_t s_ap_event_handler;
extern const char gb_manager_html_start[] asm("_binary_gb_manager_html_start");

static void ap_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Wi-Fi client connected to management AP");
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = data;
        ESP_LOGW(TAG, "Wi-Fi client disconnected, reason=%u", event->reason);
    }
}

static esp_err_t page_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "HTTP GET /");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, gb_manager_html_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t list_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    char head[160];
    snprintf(head, sizeof(head), "{\"used\":%lu,\"capacity\":%lu,\"available\":%lu,\"games\":[",
             (unsigned long)gb_storage_used(),
             (unsigned long)gb_storage_limit(),
             (unsigned long)gb_storage_available());
    httpd_resp_sendstr_chunk(req, head);
    for (size_t i = 0; i < gb_storage_count(); i++) {
        const gb_rom_entry_t *entry = gb_storage_entry(i);
        if (!entry) continue;
        char escaped[sizeof(entry->name) * 6 + 1];
        size_t used = 0;
        for (size_t j = 0; entry->name[j] && j < sizeof(entry->name); j++) {
            unsigned char c = entry->name[j];
            if (c == '"' || c == '\\') {
                escaped[used++] = '\\'; escaped[used++] = c;
            } else if (c < 0x20) {
                int n = snprintf(escaped + used, sizeof(escaped) - used, "\\u%04X", c);
                used += n > 0 ? (size_t)n : 0;
            } else escaped[used++] = c;
        }
        escaped[used] = 0;
        char item[sizeof(escaped) + 64];
        snprintf(item, sizeof(item), "%s{\"name\":\"%s\",\"size\":%lu}",
                 i ? "," : "", escaped, (unsigned long)entry->size);
        httpd_resp_sendstr_chunk(req, item);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static int receive_chunk(void *context, void *dst, size_t max_bytes) {
    httpd_req_t *req = (httpd_req_t *)context;
    int got = httpd_req_recv(req, (char *)dst, max_bytes);
    return got == HTTPD_SOCK_ERR_TIMEOUT ? -1 : got;
}

static bool same_origin(httpd_req_t *req) {
    size_t length = httpd_req_get_hdr_value_len(req, "Origin");
    if (!length) return true; // native tools need no browser Origin header
    char origin[48];
    if (length >= sizeof(origin) ||
        httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK)
        return false;
    return !strcmp(origin, "http://192.168.4.1");
}

static esp_err_t upload_handler(httpd_req_t *req) {
    if (!same_origin(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "拒绝跨站请求");
    }
    char query[128], encoded[96], name[32];
    if (req->content_len < 32768 || (size_t)req->content_len > gb_storage_limit() ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", encoded, sizeof(encoded)) != ESP_OK ||
        !gb_name_url_decode(encoded, name, sizeof(name))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "文件大小或名称无效");
    }
    bool ok = gb_storage_upload(name, req->content_len, receive_chunk, req);
    if (!ok) httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, ok ? "上传成功" : "上传失败：格式、容量或名称无效，名称不能重复");
}

static esp_err_t rename_handler(httpd_req_t *req) {
    if (!same_origin(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "拒绝跨站请求");
    }
    char query[128], id_text[16], encoded[96], name[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "id", id_text, sizeof(id_text)) != ESP_OK ||
        httpd_query_key_value(query, "name", encoded, sizeof(encoded)) != ESP_OK ||
        !gb_name_url_decode(encoded, name, sizeof(name))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "名称无效：最多 31 字节，约 10 个汉字");
    }
    char *end;
    unsigned long index = strtoul(id_text, &end, 10);
    if (!id_text[0] || *end || index >= gb_storage_count() ||
        !gb_storage_rename(index, name)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "重命名失败：名称可能重复");
    }
    return httpd_resp_sendstr(req, "名称已更新，存档保持不变");
}

static esp_err_t delete_handler(httpd_req_t *req) {
    if (!same_origin(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "拒绝跨站请求");
    }
    char query[32], id_text[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "id", id_text, sizeof(id_text)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "无效编号");
    }
    char *end;
    unsigned long index = strtoul(id_text, &end, 10);
    const gb_rom_entry_t *entry = index < gb_storage_count() ? gb_storage_entry(index) : NULL;
    if (!id_text[0] || *end || !entry) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "无效游戏编号");
    }
    uint8_t hash[32];
    memcpy(hash, entry->sha256, sizeof(hash));
    bool shared_save = false;
    for (size_t i = 0; i < gb_storage_count(); i++) {
        const gb_rom_entry_t *other = gb_storage_entry(i);
        if (i != index && !memcmp(other->sha256, hash, sizeof(hash))) shared_save = true;
    }
    if (!shared_save && !gb_saves_ready()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "存档空间不可用，游戏未删除");
    }
    if (!gb_storage_delete(index)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "游戏目录写入失败，游戏未删除");
    }
    if (shared_save) return httpd_resp_sendstr(req, "游戏已删除；相同 ROM 仍在，存档共用并保留");
    if (!gb_saves_delete(hash)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "游戏已删除，存档清理失败；重进游戏管理会重试");
    }
    return httpd_resp_sendstr(req, "游戏和对应存档均已删除");
}

gb_web_status_t gb_web_status(void) { return s_status; }

bool gb_web_start(void) {
    if (s_status.running) return true;
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) goto fail;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) goto fail;
    s_ap = esp_netif_create_default_wifi_ap();
    if (!s_ap) goto fail;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    e = esp_wifi_init(&init);
    if (e != ESP_OK) goto fail;
    s_wifi_initialized = true;
    e = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (e != ESP_OK) goto fail;
    e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            ap_event, NULL, &s_ap_event_handler);
    if (e != ESP_OK) goto fail;
    e = esp_wifi_set_mode(WIFI_MODE_AP);
    if (e != ESP_OK) goto fail;
    snprintf(s_status.ssid, sizeof(s_status.ssid), "FoloToy-GB");
    wifi_config_t config = {.ap = {.channel = 1, .max_connection = 2,
                                    .authmode = WIFI_AUTH_OPEN,
                                    .pmf_cfg = {.required = false}}};
    memcpy(config.ap.ssid, s_status.ssid, strlen(s_status.ssid));
    config.ap.ssid_len = strlen(s_status.ssid);
    e = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (e != ESP_OK) goto fail;
    e = esp_wifi_start();
    if (e != ESP_OK) goto fail;
    s_wifi_started = true;
    httpd_config_t server = HTTPD_DEFAULT_CONFIG();
    server.stack_size = 8192;
    server.max_uri_handlers = 5;
    server.max_open_sockets = 4;
    server.lru_purge_enable = true;
    server.recv_wait_timeout = 5;
    server.send_wait_timeout = 5;
    e = httpd_start(&s_server, &server);
    if (e != ESP_OK) goto fail;
    httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page_handler},
        {.uri = "/api/games", .method = HTTP_GET, .handler = list_handler},
        {.uri = "/upload", .method = HTTP_POST, .handler = upload_handler},
        {.uri = "/delete", .method = HTTP_POST, .handler = delete_handler},
        {.uri = "/rename", .method = HTTP_POST, .handler = rename_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        e = httpd_register_uri_handler(s_server, &routes[i]);
        if (e != ESP_OK) goto fail;
    }
    s_status.running = true;
    ESP_LOGI(TAG, "Game management AP ready");
    return true;
fail:
    ESP_LOGE(TAG, "Game management AP failed: %s", esp_err_to_name(e));
    gb_web_stop();
    return false;
}

void gb_web_stop(void) {
    if (s_server) {
        esp_err_t e = httpd_stop(s_server);
        if (e != ESP_OK) ESP_LOGE(TAG, "HTTP server stop failed: %s", esp_err_to_name(e));
        s_server = NULL;
    }
    if (s_wifi_started) {
        esp_err_t e = esp_wifi_stop();
        if (e != ESP_OK) ESP_LOGE(TAG, "Wi-Fi stop failed: %s", esp_err_to_name(e));
        else s_wifi_started = false;
    }
    if (s_wifi_initialized) {
        esp_err_t e = esp_wifi_deinit();
        if (e != ESP_OK) ESP_LOGE(TAG, "Wi-Fi deinit failed: %s", esp_err_to_name(e));
        else s_wifi_initialized = false;
    }
    if (s_ap_event_handler) {
        esp_err_t e = esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, s_ap_event_handler);
        if (e != ESP_OK) ESP_LOGE(TAG, "AP event handler cleanup failed: %s", esp_err_to_name(e));
        s_ap_event_handler = NULL;
    }
    if (s_ap && !s_wifi_initialized) {
        esp_netif_destroy_default_wifi(s_ap);
        s_ap = NULL;
    }
    memset(&s_status, 0, sizeof(s_status));
}
