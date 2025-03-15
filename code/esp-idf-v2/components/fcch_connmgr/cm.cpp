// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <esp_log.h>

#include "cm_admin.h"
#include "cm_dns.h"
#include "cm_http.h"
#include "cm_mdns.h"
#include "cm_nvs.h"
#include "fcch_connmgr/cm.h"
#include "fcch_connmgr/cm_conf.h"
#include "fcch_connmgr/cm_mqtt.h"
#include "fcch_connmgr/cm_net.h"

static const char *TAG = "cm";

void cm_register_conf() {
    cm_admin_register_conf();
    cm_net_register_conf();
    cm_mqtt_register_conf();
}

void cm_init() {
    ESP_LOGI(TAG, "cm_init: start");
    cm_nvs_init();
    cm_conf_init();
    cm_conf_load();
    cm_net_init();
    cm_mdns_init();
    cm_dns_init();
    cm_mqtt_init();
    cm_http_init();
    ESP_LOGI(TAG, "cm_init: done");
}
