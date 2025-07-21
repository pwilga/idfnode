#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_tcp_port_reachable(const char *host, uint16_t port);

bool is_internet_reachable(void);
bool is_mqtt_broker_reachable(void);

#ifdef __cplusplus
}
#endif

#endif // NET_H
