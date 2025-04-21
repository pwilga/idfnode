idf_component_register(SRCS 
                       "../modules/wifi/wifi.c"
                       "../modules/utils/helpers.c"
                       "../modules/utils/debug.c"
                       "../modules/utils/config.c"
                       "../modules/monitor/udp_monitor.c"
                       "../modules/ota/ota.c"
                       "../modules/mqtt/mqtt.c"
                       "main.c"
                       REQUIRES driver esp_wifi nvs_flash lwip app_update mbedtls esp_timer mdns mqtt json
                       PRIV_REQUIRES spi_flash
                       INCLUDE_DIRS ""
                       "../modules/wifi"
                       "../modules/utils"
                       "../modules/ota"
                       "../modules/monitor"
                       "../modules/mqtt")
