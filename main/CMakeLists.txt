set(SRCS "main.c")

# Auto-include device-specific handlers based on DEVICE_PROFILE
# Usage: idf.py -DDEVICE_PROFILE=cikonesp_mesh build
if(DEFINED DEVICE_PROFILE)
    set(DEVICE_HANDLERS "${CMAKE_CURRENT_SOURCE_DIR}/../devices/${DEVICE_PROFILE}/device_handlers.c")

    if(EXISTS ${DEVICE_HANDLERS})
        list(APPEND SRCS ${DEVICE_HANDLERS})
        message(STATUS "Adding device handlers: ${DEVICE_PROFILE}/device_handlers.c")
        set(HAS_DEVICE_HANDLERS 1)
    else()
        message(WARNING "DEVICE_PROFILE=${DEVICE_PROFILE} set but device_handlers.c not found at ${DEVICE_HANDLERS}")
    endif()
else()
    message(STATUS "No DEVICE_PROFILE set - using default behavior")
endif()

idf_component_register(
    SRCS ${SRCS}
)

# Pass flag to main.c if device handlers exist
if(HAS_DEVICE_HANDLERS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HAS_DEVICE_HANDLERS=1)
endif()
