// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <string.h>
#include <sys/socket.h>

#include "fcch_connmgr/cm_conf.h"
#include "fcch_connmgr/cm_util.h"
#include "cm_admin.h"

//static const char *TAG = "cm_admin";

const char *cm_admin_password;
static cm_conf_item cm_admin_item_admin_password = {
    .slug_name = "pw", // PassWord
    .text_name = "Web Admin Password",
    .type = CM_CONF_ITEM_TYPE_PASS,
    .p_val = {.str = &cm_admin_password },
    .default_func = &cm_conf_default_str_empty,
};

static cm_conf_item *cm_admin_items[] = {
    &cm_admin_item_admin_password,
};

static cm_conf_page cm_admin_page_a = {
    .slug_name = "a", // Admin
    .text_name = "Admininstration",
    .items = cm_admin_items,
    .items_count = ARRAY_SIZE(cm_admin_items),
};

void cm_admin_register_conf() {
    cm_conf_register_page(&cm_admin_page_a);
}

bool cm_admin_is_protected() {
    return cm_admin_password && cm_admin_password[0];
}
