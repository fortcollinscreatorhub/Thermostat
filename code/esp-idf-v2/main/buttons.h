// Copyright 2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

enum buttons_id {
    BUTTONS_ID_START,
    BUTTONS_ID_CANCEL,
    BUTTONS_ID_SET,
    BUTTONS_ID_UP,
    BUTTONS_ID_DOWN,
};

extern void buttons_init();
// Outbound callbacks:
void buttons_on_click(buttons_id id);
