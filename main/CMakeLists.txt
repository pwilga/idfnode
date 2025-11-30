idf_component_register(
    SRCS "main.c"
    REQUIRES
        cikon_supervisor
        cikon_supervisor_inet
        cikon_supervisor_rf433
        # cikon_supervisor_zigbee  # uncomment to enable
    PRIV_REQUIRES
        cikon_helpers
)
