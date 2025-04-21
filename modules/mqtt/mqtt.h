#ifndef MQTT_H
#define MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "mqtt_client.h"

#define TELEMETRY_INTERVAL_MS 2000

#define MQTT_RX_BUFFER_SIZE 1024

// Basic identification data
#define MQTT_NAME "idfnode"
#define MQTT_CLIENT_ID "AABBCC112233" // <- UPPERCASE MAC without colons

// Home Assistant MQTT Discovery prefix
#define MQTT_DISCOVERY_PREFIX "homeassistant"

// MQTT Topics
#define MQTT_BASE_TOPIC MQTT_NAME "/" MQTT_CLIENT_ID

#define MQTT_COMMAND_TOPIC MQTT_BASE_TOPIC "/cmnd"
#define MQTT_STATUS_TOPIC MQTT_BASE_TOPIC "/stat"
#define MQTT_TELEMETRY_TOPIC MQTT_BASE_TOPIC "/tele"
#define MQTT_AVAILABILITY_TOPIC MQTT_BASE_TOPIC "/aval"

void mqtt_init();
void mqtt_shutdown();
// void mqtt_task(void *args);
void publish_ha_discovery_sensor(esp_mqtt_client_handle_t client);

void telemetry_task(void *args);
void command_task(void *args);

#ifdef __cplusplus
}
#endif

#endif // MQTT_H
