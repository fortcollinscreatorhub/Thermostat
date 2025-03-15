// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <esp_log.h>
#include <mdns.h>

#include "fcch_connmgr/cm_net.h"
#include "cm_mdns.h"

static const char *TAG = "cm_mdns";

void cm_mdns_init() {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(cm_net_hostname));
    ESP_LOGI(TAG, "Listening");
}
