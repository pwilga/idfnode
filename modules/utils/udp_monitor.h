#ifndef UDP_MONITOR_H
#define UDP_MONITOR_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <stdarg.h>

    /**
     * @brief Starts the UDP monitor task (creates FreeRTOS task).
     * Must be called once after Wi-Fi is connected.
     */
    void udp_monitor_start(void);

    /**
     * @brief Send a raw string over UDP to the connected client.
     */
    void udp_monitor_send(const char *msg);

    /**
     * @brief vprintf-compatible logger that sends logs via UDP.
     */
    int udp_monitor_vprintf(const char *fmt, va_list args);

#ifdef __cplusplus
}
#endif

#endif // UDP_MONITOR_H
