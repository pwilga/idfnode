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
    if(DEFINED ENV{DEVICE_PROFILE})
        set(DEVICE_PROFILE "$ENV{DEVICE_PROFILE}")
        message(STATUS "DEVICE_PROFILE from environment: ${DEVICE_PROFILE}")
    else()
        set(DEVICE_PROFILE "cikonesp")
        message(WARNING "DEVICE_PROFILE not set, defaulting to ${DEVICE_PROFILE}")
    endif()
endif()

if(NOT DEFINED DEVICE_PROFILE_VARIANT AND DEFINED ENV{DEVICE_PROFILE_VARIANT})
    set(DEVICE_PROFILE_VARIANT "$ENV{DEVICE_PROFILE_VARIANT}")
    message(STATUS "DEVICE_PROFILE_VARIANT from environment: ${DEVICE_PROFILE_VARIANT}")
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

# Staging directory for the LittleFS image. Components (e.g. cikon_http) copy
# their own assets here; certs are copied below. The image is created after
# project() so the tree is fully populated first.
set(LITTLEFS_IMAGE_DIR "${CMAKE_BINARY_DIR}/littlefs_image")
idf_build_set_property(LITTLEFS_IMAGE_DIR "${LITTLEFS_IMAGE_DIR}")

# Load sdkconfig.defaults from both root and device profile
# ESP-IDF automatically adds .${IDF_TARGET} variants (e.g., .esp32) after each file
# Order: base -> base.esp32 -> device -> device.esp32
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
    "${DEVICE_DIR}/sdkconfig.defaults"
)

# Use device-specific partition table if it exists
if(EXISTS "${DEVICE_DIR}/partition_table.csv")
    set(GENERATED_PT_CONFIG "${CMAKE_BINARY_DIR}/device_partition_table.defaults")
    file(WRITE "${GENERATED_PT_CONFIG}" "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"devices/${DEVICE_PROFILE}/partition_table.csv\"\n")
    list(APPEND SDKCONFIG_DEFAULTS "${GENERATED_PT_CONFIG}")
    message(STATUS "Using device-specific partition table: devices/${DEVICE_PROFILE}/partition_table.csv")
endif()

# Add variant-specific config if DEVICE_PROFILE_VARIANT is specified
if(DEFINED DEVICE_PROFILE_VARIANT)
    set(VARIANT_CONFIG "${DEVICE_DIR}/sdkconfig.defaults.${DEVICE_PROFILE_VARIANT}")
    if(EXISTS "${VARIANT_CONFIG}")
        list(APPEND SDKCONFIG_DEFAULTS "${VARIANT_CONFIG}")
        message(STATUS "Using profile variant: ${DEVICE_PROFILE_VARIANT}")
    else()
        message(WARNING "DEVICE_PROFILE_VARIANT='${DEVICE_PROFILE_VARIANT}' specified but config not found: ${VARIANT_CONFIG}")
    endif()
endif()

# Dev overlay — loaded by default, skipped only for release builds
# Usage: BUILD_TYPE=release idf.py build
if(NOT DEFINED BUILD_TYPE)
    if(DEFINED ENV{BUILD_TYPE})
        set(BUILD_TYPE "$ENV{BUILD_TYPE}")
    else()
        set(BUILD_TYPE "dev")
    endif()
endif()

if(NOT BUILD_TYPE STREQUAL "release")
    set(DEV_OVERLAY "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.dev")
    if(EXISTS "${DEV_OVERLAY}")
        list(APPEND SDKCONFIG_DEFAULTS "${DEV_OVERLAY}")
        message(STATUS "Dev build — loading sdkconfig.defaults.dev")
    endif()
else()
    message(STATUS "Release build — skipping dev overlay")
endif()

message(STATUS "SDKCONFIG_DEFAULTS: ${SDKCONFIG_DEFAULTS}")

idf_build_set_property(MINIMAL_BUILD ON)

project(idfnode)

# ---------------------------------------------------------------------------
# LittleFS image: copy the device's certs onto the partition and flash it
# together with the firmware, so nothing has to be uploaded by hand.
# Component-level assets (e.g. web pages) are staged by the owning component
# into LITTLEFS_IMAGE_DIR.
# ---------------------------------------------------------------------------
if(EXISTS "${DEVICE_CERTS_DIR}")
    file(GLOB CERT_FILES "${DEVICE_CERTS_DIR}/*")
    file(COPY ${CERT_FILES} DESTINATION "${LITTLEFS_IMAGE_DIR}/certs")
    message(STATUS "LittleFS image: bundling certs from ${DEVICE_CERTS_DIR}")
endif()

if(EXISTS "${LITTLEFS_IMAGE_DIR}")
    # FLASH_IN_PROJECT: written by `idf.py flash` alongside the firmware.
    littlefs_create_partition_image(littlefs "${LITTLEFS_IMAGE_DIR}" FLASH_IN_PROJECT)
    message(STATUS "LittleFS image from: ${LITTLEFS_IMAGE_DIR}")
endif()
