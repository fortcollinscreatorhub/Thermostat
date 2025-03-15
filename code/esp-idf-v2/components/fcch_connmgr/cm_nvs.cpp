// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <iostream>
#include <inttypes.h>
#include <sstream>
#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "cm_nvs.h"

static const char *TAG = "cm_nvs";

static nvs_handle_t cm_nvs_handle;

void cm_nvs_init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_LOGI(TAG, "Erasing NVS flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = nvs_open("rfid", NVS_READWRITE, &cm_nvs_handle);
    ESP_ERROR_CHECK(err);
}

void cm_nvs_wipe() {
    ESP_ERROR_CHECK(nvs_flash_erase());
    cm_nvs_init();
}

esp_err_t cm_nvs_export(std::stringstream &config) {
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find_in_handle(cm_nvs_handle, NVS_TYPE_ANY, &it);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        config << info.key;
        config << ' ';
        switch (info.type) {
            case NVS_TYPE_U32: {
                uint32_t val;
                err = cm_nvs_read_u32(info.key, &val);
                if (err == ESP_OK)
                    config << "u32 " << val;
                break;
            }
            case NVS_TYPE_U16: {
                uint16_t val;
                err = cm_nvs_read_u16(info.key, &val);
                if (err == ESP_OK)
                    config << "u16 " << val;
                break;
            }
            case NVS_TYPE_STR: {
                const char *val;
                err = cm_nvs_read_str(info.key, &val);
                if (err == ESP_OK)
                    config << "str " << val;
                break;
            }
            default: {
                err = ESP_ERR_NOT_SUPPORTED;
                break;
            }
        }
        config << '\n';
        if (err != ESP_OK)
            break;
        err = nvs_entry_next(&it);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND)
        err = ESP_OK;
    if (it != NULL)
        nvs_release_iterator(it);

    return err;
}

static esp_err_t cm_nvs_import_line(char *line) {
    if (*line == '\0')
        return ESP_OK;

    char *p = line;

    char *name = p;
    char *space_after_name = strchr(p, ' ');
    if (space_after_name == NULL) {
        ESP_LOGW(TAG, "space_after_name == NULL");
        return ESP_ERR_NOT_FOUND;
    }
    *space_after_name = '\0';
    p = space_after_name + 1;

    char *type = p;
    char *space_after_type = strchr(p, ' ');
    if (space_after_type == NULL) {
        ESP_LOGW(TAG, "space_after_type == NULL");
        return ESP_ERR_NOT_FOUND;
    }
    *space_after_type = '\0';
    p = space_after_type + 1;

    char *value = p;

    if (!strcmp(type, "str")) {
        return cm_nvs_write_str(name, value);
    } else if (!strcmp(type, "u32")) {
        uint32_t u32 = strtoull(value, NULL, 10);
        return cm_nvs_write_u32(name, u32);
    } else if (!strcmp(type, "u16")) {
        uint32_t u32 = strtoull(value, NULL, 10);
        uint16_t u16 = (uint16_t)u32;
        return cm_nvs_write_u16(name, u16);
    } else {
        ESP_LOGW(TAG, "Unknown field type %s", type);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t cm_nvs_import(char *config) {
    cm_nvs_wipe();

    char *p = config;
    while (true) {
        char *line = p;
        char *nl = strchr(p, '\n');
        bool eof = (nl == NULL);
        if (!eof)
            *nl = '\0';
        esp_err_t err = cm_nvs_import_line(line);
        if (err != ESP_OK)
            return err;
        if (eof)
            break;
        p = nl + 1;
    }

    return ESP_OK;
}

esp_err_t cm_nvs_read_str(const char* name, const char **p_val) {
    *p_val = NULL;

    size_t length = 0;
    esp_err_t err = nvs_get_str(cm_nvs_handle, name, NULL, &length);
    if (err != ESP_OK)
        return err;

    size_t buf_length = length + 1;
    char *val = (char *)malloc(buf_length);
    if (val == NULL)
        return ESP_ERR_NO_MEM;

    err = nvs_get_str(cm_nvs_handle, name, val, &length);
    if (err != ESP_OK) {
        free(val);
        return err;
    }

    assert(length < buf_length);
    val[length] = '\0';
    *p_val = val;
    return ESP_OK;
}

esp_err_t cm_nvs_read_u32(const char* name, uint32_t *p_val) {
    return nvs_get_u32(cm_nvs_handle, name, p_val);
}

esp_err_t cm_nvs_read_u16(const char* name, uint16_t *p_val) {
    return nvs_get_u16(cm_nvs_handle, name, p_val);
}

esp_err_t cm_nvs_write_str(const char* name, const char *val) {
    return nvs_set_str(cm_nvs_handle, name, val);
}

esp_err_t cm_nvs_write_u32(const char* name, uint32_t val) {
    return nvs_set_u32(cm_nvs_handle, name, val);
}

esp_err_t cm_nvs_write_u16(const char* name, uint16_t val) {
    return nvs_set_u16(cm_nvs_handle, name, val);
}
