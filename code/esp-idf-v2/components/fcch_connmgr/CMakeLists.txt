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
