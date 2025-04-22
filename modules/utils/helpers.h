
#ifndef HELPERS_H
#define HELPERS_H

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
// void initialise_mdns();

bool parse_bool_string(const char *input);
void dispatch_command(const char *cmd, void *args);

typedef void (*command_handler_t)(void *args);
typedef struct {
  const char *command_name;
  command_handler_t handler;
} command_entry_t;

#endif // HELPERS_H