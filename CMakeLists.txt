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
    set(EXTRA_COMPONENT_DIRS "${CIKON_COMPONENTS_PATH}")
    message(STATUS "Using local components from: ${EXTRA_COMPONENT_DIRS}")
endif()

idf_build_set_property(MINIMAL_BUILD ON)
project(idfnode)
