#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi_types_generic.h"

#include "button_manager.h"
#include "config_manager.h"
#include "debug.h"
#include "https_server.h"
// #include "net.h"
#include "cmnd.h"
#include "cmnd_basic_handlers.h"
#include "mqtt.h"
#include "ota.h"
#include "platform_services.h"
#include "supervisor.h"
#include "tele.h"
#include "tele_basic_appenders.h"
#include "udp_monitor.h"
#include "wifi.h"

#define TAG "cikon-supervisor"

#define SNTP_SYNCED_BIT BIT0

static QueueHandle_t supervisor_queue;
static EventGroupHandle_t supervisor_event_group;
typedef enum {
    SUPERVISOR_INTERVAL_1S,
    SUPERVISOR_INTERVAL_5S,
    SUPERVISOR_INTERVAL_60S,
    SUPERVISOR_INTERVAL_5M,
    SUPERVISOR_INTERVAL_10M,
    SUPERVISOR_INTERVAL_2H,
    SUPERVISOR_INTERVAL_12H,
    SUPERVISOR_INTERVAL_COUNT
} supervisor_interval_stage_t;

static const uint32_t supervisor_intervals_ms[SUPERVISOR_INTERVAL_COUNT] = {
    [SUPERVISOR_INTERVAL_1S] = 1000,
    [SUPERVISOR_INTERVAL_5S] = 5000,
    [SUPERVISOR_INTERVAL_60S] = 60000,
    [SUPERVISOR_INTERVAL_5M] = 5 * 60 * 1000,
    [SUPERVISOR_INTERVAL_10M] = 10 * 60 * 1000,
    [SUPERVISOR_INTERVAL_2H] = 2 * 60 * 60 * 1000,
    [SUPERVISOR_INTERVAL_12H] = 12 * 60 * 60 * 1000};

static void supervisor_sntp_sync_cb(struct timeval *tv) {
    xEventGroupSetBits(supervisor_event_group, SNTP_SYNCED_BIT);
}

static void supervisor_restart_cb() {

    wifi_unregister_event_handlers();

    mqtt_publish_offline_state();
    mqtt_shutdown();
}

static void supervisor_execute_stage(supervisor_interval_stage_t stage) {

    switch (stage) {
    case SUPERVISOR_INTERVAL_1S:
        break;

    case SUPERVISOR_INTERVAL_5S:
        // is_internet_reachable() ? xEventGroupSetBits(app_event_group, INTERNET_REACHABLE_BIT)
        //                         : xEventGroupClearBits(app_event_group, INTERNET_REACHABLE_BIT);
        break;

    case SUPERVISOR_INTERVAL_60S:

        break;
    case SUPERVISOR_INTERVAL_10M:
        mqtt_init();
        break;
    case SUPERVISOR_INTERVAL_2H:
        break;
    case SUPERVISOR_INTERVAL_12H:
        break;
    default:
        break;
    }
}

void supervisor_time_synced() {

    time_t now_sec = 0;
    time(&now_sec);

    struct tm tm_now = {0};
    localtime_r(&now_sec, &tm_now);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    ESP_LOGW(TAG, "Time synced: %s", buf);
}

void supervisor_task(void *args) {

    ESP_LOGI(TAG, "Supervisor task started.");

    TickType_t last_stage[SUPERVISOR_INTERVAL_COUNT];
    for (int i = 0; i < SUPERVISOR_INTERVAL_COUNT; ++i)
        last_stage[i] = xTaskGetTickCount();

    command_job_t *job;

    while (1) {
        if (xQueueReceive(supervisor_queue, &job, pdMS_TO_TICKS(100))) {
            ESP_LOGI(TAG, "Received command: %s", job->cmnd->command_id);

            job->cmnd->handler(job->args_json_str);

            free(job->args_json_str);
            free(job);
        }

        // React on envents from the event group.
        EventBits_t bits = xEventGroupGetBits(supervisor_event_group);

        if (bits & SNTP_SYNCED_BIT) {
            xEventGroupClearBits(supervisor_event_group, SNTP_SYNCED_BIT);
            supervisor_time_synced();
        }

        // Main cyclic stage execution, for each interval stage.
        TickType_t now = xTaskGetTickCount();
        for (int interval_idx = 0; interval_idx < SUPERVISOR_INTERVAL_COUNT; ++interval_idx) {
            if (now - last_stage[interval_idx] >=
                pdMS_TO_TICKS(supervisor_intervals_ms[interval_idx])) {
                supervisor_execute_stage((supervisor_interval_stage_t)interval_idx);
                last_stage[interval_idx] = now;
            }
        }
    }
}

static void supervisor_netif_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                           void *event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        sntp_service_init();
        mqtt_init();
    }

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_AP_START:
            https_init();
            break;
        case WIFI_EVENT_AP_STOP:
            https_shutdown();
            break;
        }
    }
}

const char *get_or_generate_ap_ssid(void) {

    const char *cfg_ssid = config_get()->wifi_ap_ssid;
    if (cfg_ssid && strlen(cfg_ssid) > 0) {
        return cfg_ssid;
    }

    static char ssid[33];

    const char *mac_str = get_client_id();
    const char *mac_last6 = mac_str + 6;

    snprintf(ssid, sizeof(ssid), "%s_%s", config_get()->dev_name, mac_last6);
    return ssid;
}

void supervisor_init_platform_services() {

    ESP_ERROR_CHECK(nvs_flash_safe_init());

    config_manager_init();

    wifi_credentials_t creds = {.sta_ssid = config_get()->wifi_ssid,
                                .sta_password = config_get()->wifi_pass,
                                .ap_ssid = get_or_generate_ap_ssid(),
                                .ap_password = NULL};

    wifi_configure(&creds);
    wifi_init_sta_mode();

    const char *hostname = config_get()->mdns_host;

    if (strlen(hostname) == 0) {
        hostname = config_get()->dev_name;
    }

    mdns_service_configure(hostname, config_get()->mdns_instance);
    mdns_service_init();

    core_system_init();

    set_restart_callback(supervisor_restart_cb);

    sntp_service_configure(
        (const char *[]){config_get()->sntp1, config_get()->sntp2, config_get()->sntp3},
        supervisor_sntp_sync_cb);
}

void supervisor_init() {

    supervisor_init_platform_services();

    mqtt_config_t mqtt_cfg = {.client_id = get_client_id(),
                              .mqtt_node = config_get()->mqtt_node,
                              .mqtt_broker = config_get()->mqtt_broker,
                              .mqtt_user = config_get()->mqtt_user,
                              .mqtt_pass = config_get()->mqtt_pass,
                              .mqtt_mtls_en = config_get()->mqtt_mtls_en,
                              .mqtt_max_retry = config_get()->mqtt_max_retry,
                              .mqtt_disc_pref = config_get()->mqtt_disc_pref,
                              .command_cb = cmnd_process_json,
                              .telemetry_cb = tele_append_all};

    mqtt_configure(&mqtt_cfg);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &supervisor_netif_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &supervisor_netif_event_handler, NULL, NULL));

    tcp_ota_init();
    udp_monitor_init();
    button_manager_init(0);

    static StaticQueue_t supervisor_queue_storage;
    static uint8_t
        supervisor_queue_buffer[CONFIG_SUPERVISOR_QUEUE_LENGTH * sizeof(command_job_t *)];

    supervisor_queue = xQueueCreateStatic(CONFIG_SUPERVISOR_QUEUE_LENGTH, sizeof(command_job_t *),
                                          supervisor_queue_buffer, &supervisor_queue_storage);

    if (!supervisor_queue) {
        ESP_LOGE(TAG, "Failed to create supervisor dispatcher queue!");
        return;
    }

    cmnd_init(supervisor_queue);
    cmnd_basic_handlers_register();

    tele_init();
    tele_basic_appenders_register();

    static StaticEventGroup_t supervisor_event_group_storage;

    if (supervisor_event_group == NULL) {
        supervisor_event_group = xEventGroupCreateStatic(&supervisor_event_group_storage);
    }

    if (!supervisor_event_group) {
        ESP_LOGE(TAG, "Failed to create supervisor event group!");
        return;
    }

    xTaskCreate(supervisor_task, "supervisor", CONFIG_SUPERVISOR_TASK_STACK_SIZE, NULL,
                CONFIG_SUPERVISOR_TASK_PRIORITY, NULL);

    // Only for debug purposes, not needed in production.
    xTaskCreate(debug_info_task, "debug_info", 4096, NULL, 0, NULL);
}
