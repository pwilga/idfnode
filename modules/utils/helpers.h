
#ifndef HELPERS_H
#define HELPERS_H

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

void full_nvs_flash_init();
void initialise_mdns();

#endif // HELPERS_H