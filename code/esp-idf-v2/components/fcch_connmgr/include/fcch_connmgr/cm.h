// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

typedef void cm_http_action_func();
typedef const char *cm_http_action_description_func();

extern void cm_register_conf();
extern void cm_init();
extern bool cm_admin_is_protected();
extern void cm_http_register_home_action(
    const char *name,
    cm_http_action_description_func *description,
    cm_http_action_func *function
);
