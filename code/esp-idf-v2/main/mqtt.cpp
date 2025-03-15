// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>

#include "fcch_connmgr/cm_mqtt.h"
#include "fcch_connmgr/cm_util.h"
#include "mqtt.h"

#define BLOCK_TIME (10 / portTICK_PERIOD_MS)

static const char *TAG = "mqtt";

enum mqtt_message_id {
    MQTT_MESSAGE_SEND_STATUS,
};

struct mqtt_message {
    mqtt_message_id id;
};

static QueueHandle_t mqtt_queue;

static void mqtt_on_msg_send_status(mqtt_message &msg) {
    auto status = mqtt_get_status();

    AutoFree<char> data;
    asprintf(
        &data.val,
        "{\"cur_temp_f\":\"%f\",\"target_temp_f\":\"%f\",\"heating\":%d,\"override_time_s\":%lu}",
        status.cur_temp_f, status.target_temp_f, (int)status.heating, (uint32_t)status.override_time_s);
    assert(data.val != NULL);

    cm_mqtt_publish_stat(data.val);
}

static void mqtt_task(void *pvParameters) {
    for (;;) {
        mqtt_message msg;
        assert(xQueueReceive(mqtt_queue, &msg, portMAX_DELAY) == pdTRUE);
        ESP_LOGI(TAG, "msg.id %d", msg.id);
        switch (msg.id) {
        case MQTT_MESSAGE_SEND_STATUS:
            mqtt_on_msg_send_status(msg);
            break;
        default:
            ESP_LOGE(TAG, "Unknown message: %d", (int)msg.id);
            break;
        }
    }
}

void mqtt_init() {
    mqtt_queue = xQueueCreate(2, sizeof(mqtt_message));
    assert(mqtt_queue != NULL);

    BaseType_t xRet = xTaskCreate(mqtt_task, "mqtt", 4096, NULL, 5, NULL);
    assert(xRet == pdPASS);
}

void mqtt_send_status() {
    mqtt_message msg{
        .id = MQTT_MESSAGE_SEND_STATUS,
    };
    assert(xQueueSend(mqtt_queue, &msg, BLOCK_TIME) == pdTRUE);
}
