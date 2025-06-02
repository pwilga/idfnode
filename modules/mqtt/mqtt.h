#ifndef MQTT_H
#define MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "mqtt_client.h"
#include "platform_services.h"

#define TELEMETRY_INTERVAL_MS 5000
#define MQTT_RX_BUFFER_SIZE 1024
#define MQTT_QOS 0

void mqtt_init();

/**
 * @brief Initiates the shutdown sequence for the MQTT client.
 *
 * This function creates a separate FreeRTOS task (`mqtt_shutdown_worker`) to perform
 * a safe and complete shutdown of the MQTT subsystem. It is designed this way to avoid
 * the risk of a task deleting itself or another currently executing task.
 *
 * The shutdown sequence includes:
 * - Terminating MQTT-related tasks (e.g., command and telemetry handlers),
 * - Deleting the internal MQTT message queue,
 * - Publishing the "offline" availability message to the broker,
 * - Stopping and destroying the MQTT client,
 * - Clearing MQTT-related status bits in the event group.
 *
 * @note
 * This function captures the current task handle (`xTaskGetCurrentTaskHandle()`)
 * and passes it to the shutdown worker. The worker will not attempt to delete
 * the calling task, preventing undefined behavior caused by one task deleting
 * another that is still executing (which would result in system instability or crashes).
 *
 * @warning
 * Never call esp_mqtt_client_destroy() directly from within a task that uses the MQTT client,
 * as it may lead to resource corruption or assertion failures. Always call this function instead.
 */
void mqtt_shutdown();

void telemetry_task(void *args);
void command_task(void *args);

void mqtt_publish(const char *topic, const char *payload, int qos, bool retain);

static inline void get_mqtt_topic(char *buf, size_t buf_size, const char *suffix) {
    snprintf(buf, buf_size, "%s/%s/%s", CONFIG_MQTT_NODE_NAME, get_client_id(), suffix);
}

#define MQTT_COMMAND_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "cmnd")
#define MQTT_STATUS_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "stat")
#define MQTT_TELEMETRY_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "tele")
#define MQTT_AVAILABILITY_TOPIC(buf) get_mqtt_topic((buf), sizeof(buf), "aval")

#ifdef __cplusplus
}
#endif

#endif // MQTT_H
