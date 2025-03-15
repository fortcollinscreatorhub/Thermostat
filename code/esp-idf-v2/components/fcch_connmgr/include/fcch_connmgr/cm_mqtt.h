// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

struct cm_mqtt_info {
    bool enabled;
    bool connected;
};

extern uint16_t cm_mqtt_status_period;
extern const char *cm_mqtt_client_name;

extern void cm_mqtt_register_conf();
extern void cm_mqtt_init();
extern void cm_mqtt_start();
extern cm_mqtt_info cm_mqtt_get_info();
extern void cm_mqtt_publish_stat(const char *data);
