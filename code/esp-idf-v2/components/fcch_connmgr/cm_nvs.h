// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#pragma once

#include <sstream>
#include <esp_err.h>
#include <nvs.h>

extern void cm_nvs_init();
extern void cm_nvs_wipe();
extern esp_err_t cm_nvs_export(std::stringstream &config);
extern esp_err_t cm_nvs_import(char *config);
extern esp_err_t cm_nvs_read_str(const char* name, const char **p_val);
extern esp_err_t cm_nvs_read_u32(const char* name, uint32_t *p_val);
extern esp_err_t cm_nvs_read_u16(const char* name, uint16_t *p_val);
extern esp_err_t cm_nvs_write_str(const char* name, const char *val);
extern esp_err_t cm_nvs_write_u32(const char* name, uint32_t val);
extern esp_err_t cm_nvs_write_u16(const char* name, uint16_t val);
