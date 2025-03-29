#ifndef WIFI_H
#define WIFI_H

#include "esp_wifi.h"
#include "freertos/event_groups.h"

/* FreeRTOS event group to signal when we are connected*/
extern EventGroupHandle_t wifi_event_group;

/* Bits definition that we can wait for.
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

void wifi_sta_init();
void print_hello();

#endif // WIFI_H