#ifndef WIFI_H
#define WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

// #include <esp_wifi_types_generic.h>

void wifi_stack_init();
void wifi_ensure_sta_mode();
void wifi_ensure_ap_mode();
/**
 * @brief Completely disables WiFi (STA and AP). If STA is connected, disconnects first.
 * @return esp_err_t result of the last WiFi operation
 */
esp_err_t safe_wifi_stop();

#ifdef __cplusplus
}
#endif

#endif // WIFI_H