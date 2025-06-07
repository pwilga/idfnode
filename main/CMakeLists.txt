idf_component_register(
    SRCS
        "../modules/wifi/wifi.c"
        "../modules/utils/debug.c"
        "../modules/core/platform_services.c"
        "../modules/monitor/udp_monitor.c"
        "../modules/ota/ota.c"
        "../modules/mqtt/mqtt.c"
        "../modules/mqtt/ha.c"
        "../modules/supervisor/supervisor.c"
        "../modules/utils/json_parser.c"
        "../modules/utils/config_manager.c"
        "main.c"
    REQUIRES driver esp_wifi nvs_flash lwip app_update mbedtls esp_timer mdns mqtt json
    PRIV_REQUIRES spi_flash
    INCLUDE_DIRS
        "../modules/core"
        "../modules/wifi"
        "../modules/utils"
        "../modules/ota"
        "../modules/monitor"
        "../modules/mqtt"
        "../modules/supervisor"
    EMBED_TXTFILES
        "certs/ca.crt"
        "certs/cikonesp.crt"
        "certs/cikonesp.key"
)
