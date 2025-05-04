#ifndef MQTT_H
#define MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "helpers.h"
#include "mqtt_client.h"

#define TELEMETRY_INTERVAL_MS 5000
#define MQTT_RX_BUFFER_SIZE 1024
#define MQTT_NAME "idfnode"

void mqtt_init();
void mqtt_shutdown();

void telemetry_task(void *args);
void command_task(void *args);

static inline void get_mqtt_topic(char *buf, size_t buf_size,
                                  const char *suffix) {
  snprintf(buf, buf_size, "%s/%s/%s", MQTT_NAME, get_client_id(), suffix);
}

#define MQTT_COMMAND_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "cmnd")
#define MQTT_STATUS_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "stat")
#define MQTT_TELEMETRY_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "tele")
#define MQTT_AVAILABILITY_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "aval")

#ifdef __cplusplus
}
#endif

#endif // MQTT_H
