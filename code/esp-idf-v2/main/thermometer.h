// Copyright 2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

extern void thermometer_init();
extern void thermometer_request_conversion();
// Outbound callbacks:
void thermometer_on_error();
void thermometer_on_temp_f(float temp_f);
