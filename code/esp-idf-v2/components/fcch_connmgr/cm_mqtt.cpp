// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <string.h>

#include <esp_log.h>
#include <mqtt_client.h>

#include "fcch_connmgr/cm_conf.h"
#include "fcch_connmgr/cm_mqtt.h"
#include "fcch_connmgr/cm_net.h"
#include "fcch_connmgr/cm_util.h"

static const char *TAG = "cm_mqtt";

static bool cm_mqtt_connected;
static esp_mqtt_client_handle_t cm_mqtt_client;
static char *cm_mqtt_topic_stat;

static const char *cm_mqtt_hostname;
static cm_conf_item cm_mqtt_item_hostname = {
    .slug_name = "h", // Host name
    .text_name = "Server Host Name",
    .type = CM_CONF_ITEM_TYPE_STR,
    .p_val = {.str = &cm_mqtt_hostname },
    .default_func = &cm_conf_default_str_empty,
};

static uint16_t cm_mqtt_port;
static cm_conf_item cm_mqtt_item_port = {
    .slug_name = "p", // Port
    .text_name = "Server Port",
    .type = CM_CONF_ITEM_TYPE_U16,
    .p_val = {.u16 = &cm_mqtt_port },
    .default_func = &cm_conf_default_u16_0,
};

static void cm_mqtt_default_to_hostname(cm_conf_item *item, cm_conf_p_val p_val_u) {
    *p_val_u.str = strdup(cm_net_hostname);
}

const char *cm_mqtt_client_name;
static cm_conf_item cm_mqtt_item_client_name = {
    .slug_name = "c", // Client name
    .text_name = "Client Name",
    .type = CM_CONF_ITEM_TYPE_STR,
    .p_val = {.str = &cm_mqtt_client_name },
    .default_func = &cm_mqtt_default_to_hostname,
};

static const char *cm_mqtt_topic;
static cm_conf_item cm_mqtt_item_topic = {
    .slug_name = "t", // Topic
    .text_name = "Topic",
    .type = CM_CONF_ITEM_TYPE_STR,
    .p_val = {.str = &cm_mqtt_topic },
    .default_func = &cm_mqtt_default_to_hostname,
};

uint16_t cm_mqtt_status_period;
static cm_conf_item cm_mqtt_item_status_period = {
    .slug_name = "sp", // Status Period
    .text_name = "Status Period",
    .type = CM_CONF_ITEM_TYPE_U16,
    .p_val = {.u16 = &cm_mqtt_status_period },
    .default_func = &cm_conf_default_u16_0,
};

static cm_conf_item *cm_mqtt_items[] = {
    &cm_mqtt_item_hostname,
    &cm_mqtt_item_port,
    &cm_mqtt_item_client_name,
    &cm_mqtt_item_topic,
    &cm_mqtt_item_status_period,
};

static cm_conf_page cm_mqtt_page = {
    .slug_name = "mq", // MQtt
    .text_name = "MQTT",
    .items = cm_mqtt_items,
    .items_count = ARRAY_SIZE(cm_mqtt_items),
};

static void cm_mqtt_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        cm_mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        cm_mqtt_connected = false;
        break;
    default:
        ESP_LOGI(TAG, "MQTT_EVENT_? %" PRId32, event_id);
        break;
    }
}

void cm_mqtt_register_conf() {
    cm_conf_register_page(&cm_mqtt_page);
}

void cm_mqtt_init() {
    if (cm_mqtt_hostname[0] == '\0' ||
        cm_mqtt_client_name[0] == '\0' ||
        cm_mqtt_topic[0] == '\0'
    ) {
        return;
    }

    asprintf(&cm_mqtt_topic_stat, "stat/%s", cm_mqtt_topic);
    assert(cm_mqtt_topic_stat != NULL);

    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.broker.address.uri = NULL;
    mqtt_cfg.broker.address.hostname = cm_mqtt_hostname;
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    mqtt_cfg.broker.address.path = NULL;
    mqtt_cfg.broker.address.port = cm_mqtt_port;
    mqtt_cfg.credentials.client_id = cm_mqtt_client_name;
    cm_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    assert(cm_mqtt_client != NULL);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        cm_mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
        cm_mqtt_event_handler, NULL));
}

void cm_mqtt_start() {
    if (cm_mqtt_client == NULL)
        return;
    static bool started = false;
    if (started)
        return;
    started = true;
    ESP_ERROR_CHECK(esp_mqtt_client_start(cm_mqtt_client));
}

cm_mqtt_info cm_mqtt_get_info() {
    cm_mqtt_info info;
    info.enabled = cm_mqtt_client != NULL;
    info.connected = cm_mqtt_connected;
    return info;
}

void cm_mqtt_publish_stat(const char *data) {
    // Note: this is slightly racy, but network connections and message
    // transmission aren't synchronized anyway.
    if (!cm_mqtt_connected) {
        ESP_LOGE(TAG, "MQTT disconnected; dropping '%s'", data);
        return;
    }

    int ret = esp_mqtt_client_publish(cm_mqtt_client, cm_mqtt_topic_stat, data, 0, 1, 0);
    if (ret < 0) {
        ESP_LOGE(TAG, "publish failed: %d", ret);
    }
}
