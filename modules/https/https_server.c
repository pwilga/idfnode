#include "https_server.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include <esp_https_server.h>
#include <esp_log.h>
#include <esp_system.h>

#include "config_manager.h"
#include "platform_services.h"

#define TAG "cikon-https"

extern const uint8_t ca_crt_start[] asm("_binary_ca_crt_start");
extern const uint8_t ca_crt_end[] asm("_binary_ca_crt_end");
extern const uint8_t cikonesp_crt_start[] asm("_binary_cikonesp_crt_start");
extern const uint8_t cikonesp_crt_end[] asm("_binary_cikonesp_crt_end");
extern const uint8_t cikonesp_key_start[] asm("_binary_cikonesp_key_start");
extern const uint8_t cikonesp_key_end[] asm("_binary_cikonesp_key_end");

static httpd_handle_t https_server_handle = NULL;

void https_init(void) { xTaskCreate(https_server_task, "https_serv", 4096, NULL, 2, NULL); }
void https_shutdown(void) {
    if (https_server_handle != NULL)
        xEventGroupSetBits(app_event_group, HTTPS_SHUTDOWN_INITIATED_BIT);
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    // Force connection close to immediately free RAM after each request (no keep-alive)
    // For multiple/frequent requests, consider enabling keep-alive with a short timeout, e.g.:
    // httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    // conf.keep_alive_enable = true;
    // conf.keep_alive_idle = 5; // seconds (example: 5 seconds timeout)
    // conf.keep_alive_interval = 5;
    // conf.keep_alive_count = 3;
    httpd_resp_set_hdr(req, "Connection", "close");

    const char resp[] = "Cikon HTTPS server is running!";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t cmnd_post_handler(httpd_req_t *req) {

    char auth_buf[128];
    // Oczekiwany nagłówek: "Basic dXNlcjpwYXNz" (user:pass → base64)
    // Wersja bez hardkodowania: pobierz z NVS lub config_get()
    const char *expected_auth = config_get()->http_auth; // np. "Basic dXNlcjpwYXNz"

    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf)) != ESP_OK ||
        strcmp(auth_buf, expected_auth) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32\"");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t cmnd_post_uri = {
    .uri = "/cmnd", .method = HTTP_POST, .handler = cmnd_post_handler, .user_ctx = NULL};

static const httpd_uri_t root = {
    .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL};

void https_server_start(void) {
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

    conf.servercert = cikonesp_crt_start;
    conf.servercert_len = cikonesp_crt_end - cikonesp_crt_start;
    conf.prvtkey_pem = cikonesp_key_start;
    conf.prvtkey_len = cikonesp_key_end - cikonesp_key_start;
    // conf.cacert_pem = ca_crt_start;
    // conf.cacert_len = ca_crt_end - ca_crt_start;

    esp_err_t ret = httpd_ssl_start(&https_server_handle, &conf);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error starting HTTPS server (%d: %s)", ret, esp_err_to_name(ret));
        https_server_handle = NULL;
        return;
    }
    httpd_register_uri_handler(https_server_handle, &root);
    httpd_register_uri_handler(https_server_handle, &cmnd_post_uri);
}

void https_server_stop(void) {

    if (https_server_handle) {
        httpd_ssl_stop(https_server_handle);
        https_server_handle = NULL;
    } else {
        ESP_LOGW(TAG, "HTTPS server not running");
    }
}

void https_server_task(void *args) {

    https_server_start();

    while (!(xEventGroupGetBits(app_event_group) & HTTPS_SHUTDOWN_INITIATED_BIT)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    https_server_stop();
    xEventGroupClearBits(app_event_group, HTTPS_SHUTDOWN_INITIATED_BIT);
    vTaskDelete(NULL);
}
