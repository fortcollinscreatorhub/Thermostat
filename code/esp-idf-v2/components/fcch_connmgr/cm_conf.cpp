// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <esp_log.h>

#include "fcch_connmgr/cm_conf.h"
#include "cm_nvs.h"

static const char *TAG = "cm_conf";

cm_conf_page *cm_conf_pages = NULL;
static cm_conf_page **cm_conf_pages_tail = &cm_conf_pages;

void cm_conf_default_str_empty(cm_conf_item *item, cm_conf_p_val p_val_u) {
    *p_val_u.str = strdup("");
}

void cm_conf_default_u32_0(cm_conf_item *item, cm_conf_p_val p_val_u) {
    *p_val_u.u32 = 0;
}

void cm_conf_default_u16_0(cm_conf_item *item, cm_conf_p_val p_val_u) {
    *p_val_u.u16 = 0;
}

void cm_conf_init() {
}

void cm_conf_register_page(cm_conf_page *page) {
    page->next = NULL;
    *cm_conf_pages_tail = page;
    cm_conf_pages_tail = &(page->next);
}

static const char *cm_conf_item_full_name(
    cm_conf_page *page,
    cm_conf_item *item
) {
    char *ret = NULL;
    asprintf(&ret, "cm/%s/%s", page->slug_name, item->slug_name);
    assert(ret != NULL);
    return ret;
}

static const char *cm_conf_read_str_or_default(cm_conf_item *item) {
    const char *str;
    esp_err_t err = cm_nvs_read_str(item->full_name, &str);
    if (err != ESP_OK)
        item->default_func(item, {.str = &str});
    if (item->replace_invalid_value != NULL)
        item->replace_invalid_value(item, {.str = &str});
    return str;
}

static uint32_t cm_conf_read_u32_or_default(cm_conf_item *item) {
    uint32_t u32;
    esp_err_t err = cm_nvs_read_u32(item->full_name, &u32);
    if (err != ESP_OK)
        item->default_func(item, {.u32 = &u32});
    if (item->replace_invalid_value != NULL)
        item->replace_invalid_value(item, {.u32 = &u32});
    return u32;
}

static uint16_t cm_conf_read_u16_or_default(cm_conf_item *item) {
    uint16_t u16;
    esp_err_t err = cm_nvs_read_u16(item->full_name, &u16);
    if (err != ESP_OK)
        item->default_func(item, {.u16 = &u16});
    if (item->replace_invalid_value != NULL)
        item->replace_invalid_value(item, {.u16 = &u16});
    return u16;
}

void cm_conf_load() {
    for (
        cm_conf_page *page = cm_conf_pages;
        page != NULL;
        page = page->next
    ) {
        ESP_LOGD(TAG, "load page %s", page->slug_name);
        for (size_t i = 0; i < page->items_count; i++) {
            cm_conf_item *item = page->items[i];
            item->full_name = cm_conf_item_full_name(page, item);
            ESP_LOGD(TAG, "load item %s", item->full_name);
            switch (item->type) {
            case CM_CONF_ITEM_TYPE_STR:
            case CM_CONF_ITEM_TYPE_PASS:
                *(item->p_val.str) = cm_conf_read_str_or_default(item);
                ESP_LOGD(TAG, "value \"%s\"", *(item->p_val.str) ?: "(null)");
                break;
            case CM_CONF_ITEM_TYPE_U32:
                *(item->p_val.u32) = cm_conf_read_u32_or_default(item);
                ESP_LOGD(TAG, "value %lu", *(item->p_val.u32));
                break;
            case CM_CONF_ITEM_TYPE_U16:
                *(item->p_val.u16) = cm_conf_read_u16_or_default(item);
                ESP_LOGD(TAG, "value %lu", (uint32_t)*(item->p_val.u16));
                break;
            default:
                assert(false);
                break;
            }
        }
    }
}

esp_err_t cm_conf_read_as_str(cm_conf_item *item, const char **p_val) {
    switch (item->type) {
    case CM_CONF_ITEM_TYPE_STR:
    case CM_CONF_ITEM_TYPE_PASS:
        *p_val = cm_conf_read_str_or_default(item);
        return ESP_OK;
    default:
        break;
    }

    uint32_t u32;
    uint16_t u16;
    switch (item->type) {
    case CM_CONF_ITEM_TYPE_U32:
        u32 = cm_conf_read_u32_or_default(item);
        break;
    case CM_CONF_ITEM_TYPE_U16:
        u16 = cm_conf_read_u16_or_default(item);
        u32 = u16;
        break;
    default:
        assert(false);
        break;
    }
    size_t buf_len = 32;
    char *buf = (char *)malloc(buf_len);
    if (buf == NULL)
        return ESP_FAIL;
    snprintf(buf, buf_len, "%lu", u32);
    *p_val = buf;
    return ESP_OK;
}

esp_err_t cm_conf_write_as_str(cm_conf_item *item, const char *str) {
    switch (item->type) {
    case CM_CONF_ITEM_TYPE_STR:
    case CM_CONF_ITEM_TYPE_PASS:
        ESP_LOGD(TAG, "Write \"%s\" -> %s", str, item->full_name);
        return cm_nvs_write_str(item->full_name, str);
    default:
        break;
    }

    uint32_t u32 = strtoull(str, NULL, 10);
    ESP_LOGD(TAG, "Write %lu -> %s", u32, item->full_name);
    switch (item->type) {
    case CM_CONF_ITEM_TYPE_U32:
        return cm_nvs_write_u32(item->full_name, u32);
    case CM_CONF_ITEM_TYPE_U16:
        return cm_nvs_write_u16(item->full_name, u32);
    default:
        assert(false);
        break;
    }
    return ESP_OK;
}
