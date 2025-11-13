idf_component_register(
    SRCS
        "../modules/utils/debug.c"
        "../modules/monitor/udp_monitor.c"
        "../modules/supervisor/supervisor.c"
        "../modules/supervisor/cmnd.c"
        "../modules/supervisor/cmnd_core_handlers.c"
        "../modules/supervisor/tele.c"
        "../modules/supervisor/tele_basic_appenders.c"
        "../modules/supervisor/adapters/inet.c"
        "../modules/supervisor/adapters/inet_cmnd_handlers.c"
        "../modules/supervisor/adapters/zigbee.c"
        "../modules/utils/json_parser.c"
        "../modules/utils/button_manager.c"
        "../modules/utils/net.c"
        "main.c"
    REQUIRES esp_wifi cikon_tcp_ota esp_timer cjson button cikon_config cikon_mqtt cikon_core cikon_wifi cikon_https
    PRIV_REQUIRES spi_flash
    INCLUDE_DIRS
        "../modules/utils"
        "../modules/monitor"
        "../modules/supervisor"
        "../modules/supervisor/adapters"
)
