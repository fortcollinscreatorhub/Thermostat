// Copyright 2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

struct mqtt_status {
    float cur_temp_f;
    float target_temp_f;
    bool heating;
    uint16_t override_time_s;
};

extern void mqtt_init();
extern void mqtt_send_status();
// Outbound callbacks:
extern mqtt_status mqtt_get_status();
