#ifndef HA_H
#define HA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cJSON.h"
#include "stdbool.h"

typedef struct {
  char ha_config_topic[128];
  cJSON *ha_config_payload;
} ha_entity_t;

void build_ha_entity(ha_entity_t *entity, const char *entity_type,
                     const char *name);
void free_ha_entity(ha_entity_t *entity);
cJSON *build_ha_device(void);
void publish_ha_mqtt_discovery(void *args);

#ifdef __cplusplus
}
#endif

#endif // HA_H
