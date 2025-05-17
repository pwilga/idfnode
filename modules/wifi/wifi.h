#ifndef WIFI_H
#define WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_wifi_types_generic.h>

void wifi_stack_init();
void wifi_ensure_sta_mode();
void wifi_ensure_ap_mode();

#ifdef __cplusplus
}
#endif

#endif // WIFI_H