#ifndef INET_CMND_HANDLERS_H
#define INET_CMND_HANDLERS_H

/**
 * @brief Register inet adapter command handlers
 * 
 * Registers internet/network-specific commands:
 * - ap: Switch to AP mode
 * - sta: Switch to STA mode
 * - https: Control HTTPS server
 * - sntp: Control SNTP service
 * - ha: Trigger Home Assistant MQTT discovery
 */
void inet_cmnd_handlers_register(void);

#endif // INET_CMND_HANDLERS_H
