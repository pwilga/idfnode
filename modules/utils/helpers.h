
#ifndef HELPERS_H
#define HELPERS_H

#include "cJSON.h"
#include "esp_err.h"
#include "stdbool.h"
/**
 * @brief Fully initializes the NVS (Non-Volatile Storage) flash partition.
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
esp_err_t full_nvs_flash_init();
esp_err_t full_mdns_init();

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
 * @brief Returns a sanitized lowercase copy of the input string.
 *
 * Spaces are replaced with underscores, uppercase letters are lowercased.
 *
 * @param s Input null-terminated C string.
 * @return char* Newly allocated sanitized string. Must be freed by caller.
 */
char *sanitize(const char *s);

// bool parse_bool_string(const char *input);
bool parse_bool_json(cJSON *cmnd_param);
void dispatch_command(const char *cmd, void *args);
bool get_onboard_led_state(void);

typedef void (*command_handler_t)(void *args);
typedef struct {
  const char *command_name;
  command_handler_t handler;
} command_entry_t;

#endif // HELPERS_H