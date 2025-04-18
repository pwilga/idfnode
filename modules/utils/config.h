#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <esp_event_base.h>

/* Bits definition that we can wait for.
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define NETWORK_CONNECTED_BIT BIT0
#define NETWORK_FAIL_BIT BIT1

/* FreeRTOS event group to signal application state */
// extern EventGroupHandle_t wifi_event_group;
extern EventGroupHandle_t app_event_group;

extern esp_event_handler_instance_t instance_any_id;
extern esp_event_handler_instance_t instance_got_ip;

void full_esp_restart();

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H
