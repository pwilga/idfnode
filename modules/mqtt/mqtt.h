#ifndef MQTT_H
#define MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config_manager.h"
#include "mqtt_client.h"
#include "platform_services.h"

#define TELEMETRY_INTERVAL_MS 5000
#define MQTT_RX_BUFFER_SIZE 1024
#define MQTT_QOS 0

void mqtt_init(void);
void mqtt_shutdown(void);
void mqtt_publish_offline_state(void);

void mqtt_publish(const char *topic, const char *payload, int qos, bool retain);

static inline void get_mqtt_topic(char *buf, size_t buf_size, const char *suffix) {
    snprintf(buf, buf_size, "%s/%s/%s", config_get()->mqtt_node, get_client_id(), suffix);
}

#define MQTT_COMMAND_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "cmnd")
#define MQTT_STATUS_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "stat")
#define MQTT_TELEMETRY_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "tele")
#define MQTT_AVAILABILITY_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "aval")

#ifdef __cplusplus
}
#endif

#endif // MQTT_H
