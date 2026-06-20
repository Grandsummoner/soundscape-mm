cmake_minimum_required(VERSION 3.22)

# 1. Locate the MetaModule SDK directory
if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
    message("METAMODULE_SDK_DIR set by CMake variable ${METAMODULE_SDK_DIR}")
elseif (DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
    message("METAMODULE_SDK_DIR set by environment variable ${METAMODULE_SDK_DIR}")
else()
    set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/metamodule-plugin-sdk")
    message("METAMODULE_SDK_DIR set to default: ${METAMODULE_SDK_DIR}")
endif()

# 2. Include the MetaModule CMake build script
include(${METAMODULE_SDK_DIR}/plugin.cmake)

# 3. Declare the project metadata
project(maybe-Skyline
    VERSION 2.0.0
    DESCRIPTION "Skyline for MetaModule"
    LANGUAGES C CXX ASM
)

# 4. Define the library target
add_library(Skyline STATIC)

# 5. Automatically locate and compile all .cpp files inside the src/ folder
file(GLOB sources src/*.cpp)
target_sources(Skyline PRIVATE ${sources})
target_include_directories(Skyline PRIVATE src/)

# 6. Configure compiler standards
set_property(TARGET Skyline PROPERTY CXX_STANDARD 17)

# 7. Configure installation destination
if("${MM_INSTALL_DIR}" STREQUAL "")
    set(MM_INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins)
endif()

# 8. Call the built-in creator to build the final hardware-ready plugin bundle
create_plugin(
    SOURCE_LIB Skyline
    PLUGIN_NAME maybe-Skyline
    PLUGIN_JSON ${CMAKE_CURRENT_LIST_DIR}/plugin.json
    DESTINATION ${MM_INSTALL_DIR}
)
