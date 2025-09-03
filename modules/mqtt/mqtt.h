#ifndef MQTT_H
#define MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config_manager.h"

#include "platform_services.h"

void mqtt_init(void);
void mqtt_shutdown(void);
void mqtt_publish_offline_state(void);

void mqtt_publish(const char *topic, const char *payload, int qos, bool retain);
void mqtt_trigger_telemetry(void);
void mqtt_log_event_group_bits(void);

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
