#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/**
 * @file config_manager.h
 * @brief Configuration manager for persistent device settings using X-macro and NVS (ESP-IDF).
 *
 * All configuration fields are defined in one place (CONFIG_FIELDS X-macro).
 * This enables automatic generation of the config struct, setters, and NVS integration.
 *
 * IMPORTANT: Each field/key name MUST be max 15 characters (NVS key limit in ESP-IDF).
 *
 * Usage:
 *   - Call config_manager_init() at startup to load config from NVS or set defaults.
 *   - Use config_set_<field>() to update and save a single field to NVS.
 *   - Use config_manager_get() to access the current config (read-only pointer).
 *   - Use config_manager_log_all_keys() to log all NVS keys in the config namespace.
 *   - Use config_manager_erase_all() to erase all config data from NVS.
 */
#define CONFIG_FIELDS(STR, U8, U16)                                                                \
    STR(mdns_host, 32, CONFIG_MDNS_HOSTNAME)                                                       \
    STR(mdns_instance, 64, CONFIG_MDNS_INSTANCE_NAME)                                              \
    STR(mqtt_broker, 128, CONFIG_MQTT_BROKER_URI)                                                  \
    STR(mqtt_disc_pref, 32, CONFIG_MQTT_DISCOVERY_PREFIX)                                          \
    U8(mqtt_retry, CONFIG_MQTT_MAXIMUM_RETRY)                                                      \
    STR(mqtt_node, 32, CONFIG_MQTT_NODE_NAME)                                                      \
    STR(mqtt_pass, 32, CONFIG_MQTT_PASSWORD)                                                       \
    STR(mqtt_user, 32, CONFIG_MQTT_USERNAME)                                                       \
    U16(ota_tcp_port, CONFIG_OTA_TCP_PORT)                                                         \
    STR(wifi_pass, 64, CONFIG_WIFI_PASSWORD)                                                       \
    U8(wifi_max_retry, CONFIG_WIFI_MAXIMUM_RETRY)                                                  \
    STR(wifi_ssid, 32, CONFIG_WIFI_SSID)                                                           \
    U16(udp_mon_port, CONFIG_UDP_MONITOR_PORT)

/**
 * @brief Structure holding all persistent configuration fields.
 *
 * Fields are generated from CONFIG_FIELDS X-macro.
 * Do not modify directly; use setters to update and save fields.
 */
typedef struct {
#define STR(field, size, defval) char field[size];
#define U8(field, defval) uint8_t field;
#define U16(field, defval) uint16_t field;
    CONFIG_FIELDS(STR, U8, U16)
#undef STR
#undef U8
#undef U16
} config_t;

/**
 * @name Field Setters
 * @brief Set a configuration field and save it to NVS.
 *
 * Each setter updates only one field and commits the change to NVS.
 * Use these instead of modifying the struct directly.
 */
///@{
/** Set string field and save to NVS. */
#define GEN_SETTER_STR(field, size, defval) esp_err_t config_set_##field(const char *val);
/** Set uint8_t field and save to NVS. */
#define GEN_SETTER_U8(field, defval) esp_err_t config_set_##field(uint8_t val);
/** Set uint16_t field and save to NVS. */
#define GEN_SETTER_U16(field, defval) esp_err_t config_set_##field(uint16_t val);
CONFIG_FIELDS(GEN_SETTER_STR, GEN_SETTER_U8, GEN_SETTER_U16)
#undef GEN_SETTER_STR
#undef GEN_SETTER_U8
#undef GEN_SETTER_U16
///@}

/**
 * @brief Initialize configuration: load from NVS or set defaults if not present.
 *
 * Call once at startup before using any config accessors or setters.
 */
void config_manager_init(void);

/**
 * @brief Get a pointer to the current configuration (read-only).
 *
 * @return Pointer to config_t structure with current values.
 */
const config_t *config_manager_get(void);

/**
 * @brief Log all NVS keys in the configuration namespace (for diagnostics).
 */
void config_manager_log_all_keys(void);

/**
 * @brief Erase all configuration data from NVS (factory reset).
 */
void config_manager_erase_all();

#endif // CONFIG_MANAGER_H
