# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# For local development - use EXTRA_COMPONENT_DIRS if folder exists
# For production builds - components are fetched via idf_component.yml
# Override with: cmake -DCIKON_COMPONENTS_PATH=/custom/path ..
if(NOT DEFINED CIKON_COMPONENTS_PATH)
    set(CIKON_COMPONENTS_PATH "../cikon-iot-solution/components")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${CIKON_COMPONENTS_PATH}")
    set(EXTRA_COMPONENT_DIRS
        "${CIKON_COMPONENTS_PATH}"
        "${CIKON_COMPONENTS_PATH}/cikon_supervisor_adapters"
    )
    message(STATUS "Using local components from: ${EXTRA_COMPONENT_DIRS}")
endif()

if(NOT DEFINED DEVICE_PROFILE)
    set(DEVICE_PROFILE "cikonesp")
    message(WARNING "DEVICE_PROFILE not set, defaulting to ${DEVICE_PROFILE}")
endif()

set(DEVICE_DIR "${CMAKE_CURRENT_LIST_DIR}/devices/${DEVICE_PROFILE}")
if(NOT EXISTS "${DEVICE_DIR}")
    message(FATAL_ERROR "Device profile not found: ${DEVICE_DIR}")
endif()

set(DEVICE_CONFIG_DIR "${DEVICE_DIR}/config")
set(DEVICE_CERTS_DIR  "${DEVICE_DIR}/certs")

message(STATUS "Using device profile: ${DEVICE_PROFILE}")
message(STATUS "DEVICE_CONFIG_DIR: ${DEVICE_CONFIG_DIR}")
message(STATUS "DEVICE_CERTS_DIR: ${DEVICE_CERTS_DIR}")

# Make variables visible to all components
idf_build_set_property(DEVICE_CONFIG_DIR "${DEVICE_CONFIG_DIR}")
idf_build_set_property(DEVICE_CERTS_DIR "${DEVICE_CERTS_DIR}")

# Load sdkconfig.defaults from both root and device profile
# ESP-IDF automatically adds .${IDF_TARGET} variants (e.g., .esp32) after each file
# Order: base -> base.esp32 -> device -> device.esp32
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
    "${DEVICE_DIR}/sdkconfig.defaults"
)
message(STATUS "SDKCONFIG_DEFAULTS: ${SDKCONFIG_DEFAULTS}")

# Exclude mesh components for non-mesh profiles
if(DEVICE_PROFILE STREQUAL "cikonesp")
    set(EXCLUDE_COMPONENTS "cikon_mesh_lite" "espressif__iot_bridge" "espressif__mesh_lite")
    message(STATUS "Excluding mesh components for profile: ${DEVICE_PROFILE}")
    message(STATUS "EXCLUDE_COMPONENTS: ${EXCLUDE_COMPONENTS}")
endif()

idf_build_set_property(MINIMAL_BUILD ON)
project(idfnode)
