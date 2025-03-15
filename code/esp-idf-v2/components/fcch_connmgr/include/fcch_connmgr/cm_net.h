// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

#include <esp_netif.h>

struct cm_net_ap_info {
    bool enabled;
    const char* network;
    uint32_t ip;
};

struct cm_net_sta_info {
    bool connected;
    bool has_ip;
    const char* network;
    uint32_t ip;
};

extern esp_netif_t *cm_net_if_ap;
extern esp_netif_t *cm_net_if_sta;
extern const char *cm_net_hostname;

extern void cm_net_register_conf();
extern void cm_net_init();
extern void cm_net_notify_upcoming_restart();
extern cm_net_ap_info cm_net_get_ap_info();
extern cm_net_sta_info cm_net_get_sta_info();
