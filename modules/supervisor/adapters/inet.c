#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_wifi_types_generic.h"
#include "mdns.h"

#include "cmnd.h"
#include "config_manager.h"
#include "https_server.h"
#include "inet.h"
#include "inet_cmnd_handlers.h"
#include "mqtt.h"
#include "ota.h"
#include "platform_services.h"
#include "supervisor.h"
#include "tele.h"
#include "udp_monitor.h"
#include "wifi.h"

#define TAG "inet-adapter"

#define INET_EVENT_TIME_SYNCED BIT0
#define INET_EVENT_STA_READY BIT1
#define INET_EVENT_AP_READY BIT2

static const char *mdns_host = NULL;
static const char *mdns_instance = NULL;

static const char *sntp_servers[CONFIG_LWIP_SNTP_MAX_SERVERS] = {NULL};
static esp_sntp_time_cb_t sntp_cb = NULL;

static esp_event_handler_instance_t inet_wifi_handler = NULL;
static esp_event_handler_instance_t inet_ip_handler = NULL;

static bool shutdown_ota = true;

static SemaphoreHandle_t network_transition_mutex = NULL;
static volatile bool sta_services_running = false;
static volatile bool ap_services_running = false;

static void inet_adapter_init(void);
static void inet_adapter_shutdown(void);
static void inet_adapter_on_event(EventBits_t bits);
static void inet_adapter_on_interval(supervisor_interval_stage_t stage);

supervisor_platform_adapter_t inet_adapter = {.init = inet_adapter_init,
                                              .shutdown = inet_adapter_shutdown,
                                              .on_event = inet_adapter_on_event,
                                              .on_interval = inet_adapter_on_interval};

static void inet_mdns_configure(const char *hostname, const char *instance_name) {
    mdns_host = hostname;
    mdns_instance = instance_name;
}

static void inet_mdns_init(void) {

    esp_err_t ret = (mdns_init() != ESP_OK || mdns_hostname_set(mdns_host) != ESP_OK ||
                     mdns_instance_name_set(mdns_instance) != ESP_OK)
                        ? ESP_FAIL
                        : ESP_OK;

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "mDNS started with hostname: %s.local", mdns_host);
    } else {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
    }
}

static void inet_sntp_configure(const char **servers, esp_sntp_time_cb_t cb) {
    for (int i = 0; i < CONFIG_LWIP_SNTP_MAX_SERVERS; ++i) {
        if (servers[i] && strlen(servers[i]) > 0) {
            sntp_servers[i] = servers[i];
        } else {
            sntp_servers[i] = NULL;
        }
    }
    sntp_cb = cb;
}

static void inet_sntp_init(void) {
    if (memcmp(sntp_servers, (const char *[CONFIG_LWIP_SNTP_MAX_SERVERS]){NULL},
               sizeof(sntp_servers)) == 0) {
        ESP_LOGW(TAG, "No SNTP servers configured, skipping SNTP init.");
        return;
    }

    esp_sntp_config_t config = {
        .smooth_sync = false,
        .server_from_dhcp = false,
        .wait_for_sync = true,
        .start = true,
        .sync_cb = sntp_cb,
        .renew_servers_after_new_IP = false,
        .ip_event_to_renew = IP_EVENT_STA_GOT_IP,
        .index_of_first_server = 0,
        .num_of_servers = CONFIG_LWIP_SNTP_MAX_SERVERS,
        .servers = {NULL},
    };

    for (int i = 0; i < CONFIG_LWIP_SNTP_MAX_SERVERS; ++i) {
        config.servers[i] = sntp_servers[i];
    }

    esp_netif_sntp_init(&config);
    ESP_LOGI(TAG, "SNTP service initialized, servers: %s, %s, %s",
             config.servers[0] ? config.servers[0] : "none",
             config.servers[1] ? config.servers[1] : "none",
             config.servers[2] ? config.servers[2] : "none");
}

void inet_sntp_reinit(void) {
    ESP_LOGI(TAG, "Reinitializing SNTP service");
    esp_netif_sntp_deinit();
    inet_sntp_init();
}

static void inet_stop_services(void) {
    ESP_LOGI(TAG, "Stopping network services");

    mqtt_publish_offline_state();
    mqtt_shutdown();
    https_shutdown();
    // Keep mDNS running - it works for both STA and AP interfaces
    // mdns_free();
    esp_netif_sntp_deinit();

    if (shutdown_ota) {
        tcp_ota_shutdown();
    }
    // udp_monitor_shutdown();

    sta_services_running = false;
    ap_services_running = false;
}

void inet_switch_to_ap_mode(void) {
    if (xSemaphoreTake(network_transition_mutex, pdMS_TO_TICKS(100)) == pdFALSE) {
        ESP_LOGW(TAG, "Network transition already in progress, cannot switch to AP");
        return;
    }

    ESP_LOGI(TAG, "Switching to AP mode");
    inet_stop_services();
    wifi_init_ap_mode();

    xSemaphoreGive(network_transition_mutex);
}

void inet_switch_to_sta_mode(void) {
    if (xSemaphoreTake(network_transition_mutex, pdMS_TO_TICKS(100)) == pdFALSE) {
        ESP_LOGW(TAG, "Network transition already in progress, cannot switch to STA");
        return;
    }

    // ESP_LOGI(TAG, "Switching to STA mode");
    inet_stop_services();
    wifi_init_sta_mode();

    xSemaphoreGive(network_transition_mutex);
}

static void inet_sntp_sync_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP time synchronized");
    supervisor_notify_event(INET_EVENT_TIME_SYNCED);
}

static void inet_restart_cb(void) {
    ESP_LOGI(TAG, "Restart requested, shutting down inet adapter");
    shutdown_ota = false; // Don't shutdown OTA before restart
    inet_adapter_shutdown();
}

static void inet_netif_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                     void *event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        supervisor_notify_event(INET_EVENT_STA_READY);
    }

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_AP_START:
            supervisor_notify_event(INET_EVENT_AP_READY);
            break;
        case WIFI_EVENT_AP_STOP:
            // ESP_LOGI(TAG, "AP stopped");
            // Services stopped via callback before mode switch
            break;
        }
    }
}

static const char *inet_get_or_generate_ap_ssid(void) {
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

static void inet_adapter_init(void) {

    ESP_LOGI(TAG, "Initializing inet platform adapter");

    if (network_transition_mutex == NULL) {
        network_transition_mutex = xSemaphoreCreateMutex();
        if (!network_transition_mutex) {
            ESP_LOGE(TAG, "Failed to create network transition mutex!");
        }
    }

    wifi_credentials_t creds = {.sta_ssid = config_get()->wifi_ssid,
                                .sta_password = config_get()->wifi_pass,
                                .ap_ssid = inet_get_or_generate_ap_ssid(),
                                .ap_password = NULL};

    wifi_configure(&creds);

    const char *hostname = config_get()->mdns_host;
    if (strlen(hostname) == 0) {
        hostname = config_get()->dev_name;
    }

    inet_mdns_configure(hostname, config_get()->mdns_instance);

    inet_sntp_configure(
        (const char *[]){config_get()->sntp1, config_get()->sntp2, config_get()->sntp3},
        inet_sntp_sync_cb);

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

    set_restart_callback(inet_restart_cb);

    // Register callback for AP timeout -> STA mode switch
    wifi_set_ap_timeout_callback(inet_stop_services);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &inet_netif_event_handler, NULL, &inet_wifi_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &inet_netif_event_handler, NULL, &inet_ip_handler));

    wifi_init_sta_mode();
    inet_cmnd_handlers_register();
}

static void inet_adapter_shutdown(void) {
    ESP_LOGI(TAG, "Shutting down inet platform adapter");

    wifi_unregister_event_handlers();

    if (inet_ip_handler) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, inet_ip_handler);
        inet_ip_handler = NULL;
    }

    if (inet_wifi_handler) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, inet_wifi_handler);
        inet_wifi_handler = NULL;
    }

    inet_stop_services();
    mdns_free();

    if (network_transition_mutex) {
        vSemaphoreDelete(network_transition_mutex);
        network_transition_mutex = NULL;
    }

    shutdown_ota = true;
}

static void inet_adapter_on_event(EventBits_t bits) {
    if (bits & INET_EVENT_TIME_SYNCED) {
        time_t now_sec = 0;
        time(&now_sec);

        struct tm tm_now = {0};
        localtime_r(&now_sec, &tm_now);

        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
        ESP_LOGW(TAG, "Time synced: %s", buf);
    }

    if (bits & INET_EVENT_STA_READY) {

        if (sta_services_running) {
            ESP_LOGW(TAG, "STA services already running, ignoring duplicate event");
            return;
        }

        ESP_LOGI(TAG, "STA ready, starting STA services");
        inet_mdns_init();
        inet_sntp_init();
        mqtt_init();
        tcp_ota_init();
        // udp_monitor_init();

        sta_services_running = true;
        ap_services_running = false;
    }

    if (bits & INET_EVENT_AP_READY) {
        if (ap_services_running) {
            ESP_LOGW(TAG, "AP services already running, ignoring duplicate event");
            return;
        }

        ESP_LOGI(TAG, "AP ready, starting AP services");
        https_init();
        inet_mdns_init();
        tcp_ota_init();
        // udp_monitor_init();

        ap_services_running = true;
        sta_services_running = false;
    }
}

static void inet_adapter_on_interval(supervisor_interval_stage_t stage) {
    switch (stage) {
    case SUPERVISOR_INTERVAL_1S:
        break;

    case SUPERVISOR_INTERVAL_5S:
        break;

    case SUPERVISOR_INTERVAL_60S:
        break;

    case SUPERVISOR_INTERVAL_5M:
        break;

    case SUPERVISOR_INTERVAL_10M:
        // mqtt_init();
        break;

    case SUPERVISOR_INTERVAL_2H:
        break;

    case SUPERVISOR_INTERVAL_12H:
        break;

    default:
        break;
    }
}
