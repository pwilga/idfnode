#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_event_base.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* Bits definition that we can wait for.
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define NETWORK_CONNECTED_BIT BIT0
#define NETWORK_FAIL_BIT BIT1
#define MQTT_CONNECTED_BIT BIT2
#define MQTT_FAIL_BIT BIT3

/* FreeRTOS event group to signal application state */
extern EventGroupHandle_t app_event_group;

extern esp_event_handler_instance_t instance_any_id;
extern esp_event_handler_instance_t instance_got_ip;

esp_err_t app_event_init(void);
void full_esp_restart();

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H
