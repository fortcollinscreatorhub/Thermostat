# Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
# SPDX-License-Identifier: MIT

idf_component_register(
    REQUIRES
        esp_eth
        esp_event
        esp_http_server
        esp_wifi
        esp-tls
        mqtt
        nvs_flash
    SRCS
        cm.cpp
        cm_admin.cpp
        cm_app_version.cpp
        cm_conf.cpp
        cm_dns.cpp
        cm_http.cpp
        cm_mdns.cpp
        cm_mqtt.cpp
        cm_nvs.cpp
        cm_net.cpp
        cm_util.cpp
    INCLUDE_DIRS
        include
    EMBED_FILES
        styles.html
)
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    OUTPUT app_version.h app_version.nonexistent_causes_cmd_to_run_every_time.h
    DEPENDS "${COMPONENT_DIR}/gen-version-h.sh"
    WORKING_DIRECTORY "${PROJECT_DIR}"
    COMMAND "${COMPONENT_DIR}/gen-version-h.sh" "${CMAKE_CURRENT_BINARY_DIR}/app_version.h"
    VERBATIM)
add_custom_target(app_version_h DEPENDS app_version.h)
add_dependencies(${COMPONENT_LIB} app_version_h)
set_property(
    DIRECTORY "${COMPONENT_DIR}"
    APPEND PROPERTY ADDITIONAL_CLEAN_FILES app_version.h)
