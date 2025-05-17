#ifndef PLATFORM_SERVICES_H
#define PLATFORM_SERVICES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event_base.h"

/* Bits definition that we can wait for.
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_STA_CONNECTED_BIT BIT0
#define WIFI_STA_FAIL_BIT BIT1
#define WIFI_AP_STARTED_BIT BIT2
#define MQTT_CONNECTED_BIT BIT3
#define MQTT_FAIL_BIT BIT4
#define MQTT_SHUTDOWN_DONE BIT5

/* FreeRTOS event group to signal application state */
extern EventGroupHandle_t app_event_group;

extern esp_event_handler_instance_t instance_any_id;
extern esp_event_handler_instance_t instance_got_ip;

/**
 * @brief Initializes global system-wide variables such as event groups.
 *
 * Must be called during application startup before any task attempts
 * to use `app_event_group` or register related event bits.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if event group creation failed
 */
esp_err_t app_event_init(void);

/**
 * @brief Unregisters network-related event handlers and performs a full ESP
 * restart.
 *
 */
void esp_safe_restart(void *args);

/**
 * @brief Initializes the NVS (Non-Volatile Storage) flash partition.
 *
 * This function ensures that the NVS is properly initialized, even in cases
 * where the flash partition has run out of free pages or a newer NVS version
 * is detected (e.g. after firmware upgrade or flash format).
 *
 * It should be called once during system startup (before using any NVS or Wi-Fi
 * functionality).
 *
 * **Required for Wi-Fi to function**: The ESP-IDF Wi-Fi stack stores
 * configuration and calibration data in NVS. Skipping this initialization will
 * result in Wi-Fi startup failure.
 */
esp_err_t nvs_flash_safe_init();

/**
 * @brief Initializes and configures the mDNS service.
 *
 * Initializes the mDNS stack, sets the hostname and instance name
 * from Kconfig values, and starts the mDNS service.
 *
 * @return ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t init_mdns_service();

/**
 * @brief Returns the Wi-Fi MAC address as a 12-character uppercase string (no
 * colons).
 *
 * This function reads the MAC address once (on first call) and caches it in a
 * static buffer. Subsequent calls return the same pointer without re-reading
 * the hardware.
 *
 * Example return value: "A1B2C3D4E5F6"
 *
 * @return const char* Pointer to a static null-terminated string, or NULL on
 * error.
 */
const char *get_client_id();

/**
 * @brief Returns the ISO8601-formatted boot time of the system.
 *
 * Calculates the startup timestamp once and returns the cached value
 * on subsequent calls.
 *
 * @return Pointer to a static ISO8601 string (UTC).
 */
const char *get_boot_time(void);

void switch_to_ap(void *args);
#ifdef __cplusplus
}
#endif

#endif // PLATFORM_SERVICES_H
