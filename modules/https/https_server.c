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
#include "supervisor.h"

#define TAG "cikon-https"
#define HTTPS_INACTIVITY_TIMEOUT_MS 60000

extern const uint8_t ca_crt_start[] asm("_binary_ca_crt_start");
extern const uint8_t ca_crt_end[] asm("_binary_ca_crt_end");
extern const uint8_t cikonesp_crt_start[] asm("_binary_cikonesp_crt_start");
extern const uint8_t cikonesp_crt_end[] asm("_binary_cikonesp_crt_end");
extern const uint8_t cikonesp_key_start[] asm("_binary_cikonesp_key_start");
extern const uint8_t cikonesp_key_end[] asm("_binary_cikonesp_key_end");

static httpd_handle_t https_server_handle = NULL;
static TimerHandle_t https_inactivity_timer = NULL;

static void https_inactivity_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGW(TAG, "HTTPS inactivity timer: %.1f s timeout, restarting server",
             HTTPS_INACTIVITY_TIMEOUT_MS / 1000.0f);
    https_shutdown();

    while (https_server_handle != NULL)
        vTaskDelay(pdMS_TO_TICKS(10));

    https_init();
}

/**
 * @brief Restarts the inactivity timer on every valid HTTP request.
 *
 * This timer replaces the need for HTTP keep-alive: the server is configured for one connection at
 * a time and does not use keep-alive (persistent connections). Each request resets the timer, so
 * the server stays up as long as there is activity, regardless of connection reuse. This approach
 * avoids RAM leaks and blocking issues that can occur with keep-alive on resource-constrained
 * devices like ESP32. When no requests are received for HTTPS_INACTIVITY_TIMEOUT_MS, the server is
 * safely shut down and restarted, ensuring robust operation.
 */
static void https_restart_inactivity_timer(void) {
    if (https_inactivity_timer) {
        xTimerStop(https_inactivity_timer, 0);
        xTimerChangePeriod(https_inactivity_timer, pdMS_TO_TICKS(HTTPS_INACTIVITY_TIMEOUT_MS),
                           0); // <--- Set your timeout here
        xTimerStart(https_inactivity_timer, 0);
    }
}

void https_init(void) {
    if (!https_inactivity_timer) {
        // Set timer period to 1 tick (minimum, will be changed on use)
        https_inactivity_timer =
            xTimerCreate("https_inact", 1, pdFALSE, NULL, https_inactivity_timer_callback);
    }
    xTaskCreate(https_server_task, "https_serv", 4096, NULL, 2, NULL);
}

void https_shutdown(void) {
    if (https_server_handle != NULL)
        xEventGroupSetBits(app_event_group, HTTPS_SHUTDOWN_INITIATED_BIT);
}

static bool https_check_basic_auth(httpd_req_t *req) {
    size_t auth_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (auth_len == 0) {
        goto unauthorized;
    }
    char *auth_buf = malloc(auth_len + 1);
    if (!auth_buf) {
        httpd_resp_send_500(req);
        return false;
    }
    bool auth_ok =
        (httpd_req_get_hdr_value_str(req, "Authorization", auth_buf, auth_len + 1) == ESP_OK) &&
        (strcmp(auth_buf, config_get()->http_auth) == 0);
    free(auth_buf);
    if (auth_ok) {
        return true;
    }
unauthorized:
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32\"");
    httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
    return false;
}

static esp_err_t tele_get_handler(httpd_req_t *req) {
    if (!https_check_basic_auth(req)) {
        ESP_LOGW(TAG, "GET /tele: unauthorized");
        return ESP_FAIL;
    }

    https_restart_inactivity_timer();

    cJSON *json = cJSON_CreateObject();
    supervisor_state_to_json(json);
    char *json_str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t cmnd_post_handler(httpd_req_t *req) {
    if (!https_check_basic_auth(req)) {
        ESP_LOGW(TAG, "POST /cmnd: unauthorized");
        return ESP_FAIL;
    }

    https_restart_inactivity_timer();

    int total_len = req->content_len;
    char *buf = malloc(total_len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "POST /cmnd: malloc failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            ESP_LOGE(TAG, "POST /cmnd: recv failed");
            free(buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';
    ESP_LOGI(TAG, "Received command: %s", buf);
    supervisor_process_command_payload(buf);
    free(buf);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t cmnd_post_uri = {
    .uri = "/cmnd", .method = HTTP_POST, .handler = cmnd_post_handler, .user_ctx = NULL};

static const httpd_uri_t tele_get_uri = {
    .uri = "/tele", .method = HTTP_GET, .handler = tele_get_handler, .user_ctx = NULL};

void https_server_start(void) {
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

    conf.servercert = cikonesp_crt_start;
    conf.servercert_len = cikonesp_crt_end - cikonesp_crt_start;
    conf.prvtkey_pem = cikonesp_key_start;
    conf.prvtkey_len = cikonesp_key_end - cikonesp_key_start;
    // conf.cacert_pem = ca_crt_start;
    // conf.cacert_len = ca_crt_end - ca_crt_start;

    // Limit HTTPS server to only one connection at a time
    httpd_config_t httpd_conf = HTTPD_DEFAULT_CONFIG();
    httpd_conf.max_open_sockets = 1;
    // httpd_conf.keep_alive_enable = true;
    // httpd_conf.keep_alive_idle = 60; // seconds
    // httpd_conf.keep_alive_interval = 5;
    // httpd_conf.keep_alive_count = 3;
    conf.httpd = httpd_conf;

    esp_err_t ret = httpd_ssl_start(&https_server_handle, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error starting HTTPS server (%d: %s)", ret, esp_err_to_name(ret));
        https_server_handle = NULL;
        return;
    }
    httpd_register_uri_handler(https_server_handle, &tele_get_uri);
    httpd_register_uri_handler(https_server_handle, &cmnd_post_uri);
}

void https_server_stop(void) {
    if (https_server_handle) {
        httpd_ssl_stop(https_server_handle);
        https_server_handle = NULL;
    }
    if (https_inactivity_timer) {
        // Delete the inactivity timer to free resources, since the server is stopped
        xTimerDelete(https_inactivity_timer, 0);
        https_inactivity_timer = NULL;
    }
}

void https_server_task(void *args) {

    https_server_start();

    while (!(xEventGroupGetBits(app_event_group) & HTTPS_SHUTDOWN_INITIATED_BIT)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    https_server_stop();

    xEventGroupClearBits(app_event_group, HTTPS_SHUTDOWN_INITIATED_BIT);
    https_server_handle = NULL;
    vTaskDelete(NULL);
}
