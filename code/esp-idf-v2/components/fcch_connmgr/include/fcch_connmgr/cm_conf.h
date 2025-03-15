// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>

enum cm_conf_item_type {
    CM_CONF_ITEM_TYPE_STR,
    CM_CONF_ITEM_TYPE_PASS,
    CM_CONF_ITEM_TYPE_U32,
    CM_CONF_ITEM_TYPE_U16,
};

struct cm_conf_item;

union cm_conf_p_val {
    const char **str;
    uint32_t *u32;
    uint16_t *u16;
};

typedef void cm_conf_default_func(cm_conf_item *item, cm_conf_p_val p_val_u);
typedef void cm_replace_invalid_value_func(cm_conf_item *item, cm_conf_p_val p_val_u);

struct cm_conf_item {
    const char *slug_name;
    const char *text_name;
    cm_conf_item_type type;
    cm_conf_p_val p_val;
    cm_conf_default_func *default_func;
    cm_replace_invalid_value_func *replace_invalid_value = NULL;

    const char *full_name = NULL;
};

struct cm_conf_page {
    const char *slug_name;
    const char *text_name;
    cm_conf_item **items;
    size_t items_count;

    cm_conf_page *next = NULL;
};

extern cm_conf_page *cm_conf_pages;

extern cm_conf_default_func cm_conf_default_str_empty;
extern cm_conf_default_func cm_conf_default_u32_0;
extern cm_conf_default_func cm_conf_default_u16_0;

extern void cm_conf_init();
extern void cm_conf_register_page(cm_conf_page *page);
extern void cm_conf_load();

extern esp_err_t cm_conf_read_as_str(cm_conf_item *item, const char **p_val);
extern esp_err_t cm_conf_write_as_str(cm_conf_item *item, const char *str);
