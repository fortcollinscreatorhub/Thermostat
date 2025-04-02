// Copyright 2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

struct lcd_thermostat_info {
    float cur_temp_f;
    float target_temp_f;
    uint16_t override_time_s;
    bool force_heat;
};

extern void lcd_init();
extern void lcd_on_1s_timer();
extern void lcd_on_thermostat_change();
// Outbound callbacks:
extern lcd_thermostat_info lcd_get_thermostat_info();
