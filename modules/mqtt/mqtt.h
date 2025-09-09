#ifndef MQTT_H
#define MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mqtt_command_callback_t)(const char *payload);
typedef void (*mqtt_telemetry_callback_t)(cJSON *json_root);

typedef struct {
    const char *client_id;
    const char *mqtt_node;
    const char *mqtt_broker;
    const char *mqtt_user;
    const char *mqtt_pass;
    uint8_t mqtt_mtls_en;
    uint8_t mqtt_max_retry;
    mqtt_command_callback_t command_cb;
    mqtt_telemetry_callback_t telemetry_cb;
} mqtt_config_t;

void mqtt_configure(const mqtt_config_t *cfg);
void mqtt_init(void);
void mqtt_shutdown(void);

void mqtt_publish(const char *topic, const char *payload, int qos, bool retain);
void mqtt_publish_offline_state(void);
void mqtt_trigger_telemetry(void);

void mqtt_log_event_group_bits(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_H
