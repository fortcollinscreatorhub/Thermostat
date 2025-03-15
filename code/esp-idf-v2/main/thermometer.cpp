// Copyright 2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <driver/gpio.h>
#include <stdio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ds18b20.h>
#include <onewire_bus.h>
#include "thermometer.h"

#define BLOCK_TIME (10 / portTICK_PERIOD_MS)

#define THERMOMETER_ONEWIRE_BUS_GPIO GPIO_NUM_27

enum thermometer_message_id {
    THERMOMETER_MESSAGE_REQUEST_CONVERSION,
};

struct thermometer_message {
    thermometer_message_id id;
};

static const char *TAG = "thermometer";
static QueueHandle_t thermometer_queue;
static ds18b20_device_handle_t thermometer_ds18b20s;

static void thermometer_task_init() {
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = THERMOMETER_ONEWIRE_BUS_GPIO,
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10, // 1byte ROM command + 8byte ROM number + 1byte device command
    };
    onewire_bus_handle_t bus;
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));

    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_onewire_device;
    esp_err_t search_result;

    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    ESP_LOGI(TAG, "OneWire iterator created; start searching...");
    while (true) {
        search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (search_result == ESP_ERR_NOT_FOUND)
            break;
        if (search_result != ESP_OK)
            continue;

        ds18b20_config_t ds_cfg = {};
        // Check if the device is a DS18B20, if so, return the ds18b20 handle
        if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &thermometer_ds18b20s) != ESP_OK) {
            ESP_LOGI(TAG, "Found an unknown device, address: %016llX", next_onewire_device.address);
            continue;
        }

        ESP_LOGI(TAG, "Found a DS18B20, address: %016llX", next_onewire_device.address);
        ESP_ERROR_CHECK(ds18b20_set_resolution(thermometer_ds18b20s, DS18B20_RESOLUTION_12B));
        break;
    }
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    assert(search_result == ESP_OK);
}

static void thermometer_on_msg_request_conversion() {
    esp_err_t err;
    err = ds18b20_trigger_temperature_conversion(thermometer_ds18b20s);
    if (err != ESP_OK)
        thermometer_on_error();

    float temp_c;
    err = ds18b20_get_temperature(thermometer_ds18b20s, &temp_c);
    if (err != ESP_OK)
        thermometer_on_error();

    float temp_f = (temp_c * (9.0f / 5.0f)) + 32;
    ESP_LOGD(TAG, "temp_c, temp_f: %.2f, %.2fF", temp_c, temp_f);
    thermometer_on_temp_f(temp_f);
}

static void thermometer_task(void *pvParameters) {
    thermometer_task_init();

    for (;;) {
        thermometer_message msg;
        assert(xQueueReceive(thermometer_queue, &msg, portMAX_DELAY) == pdTRUE);
        ESP_LOGD(TAG, "msg.id %d", msg.id);
        switch (msg.id) {
        case THERMOMETER_MESSAGE_REQUEST_CONVERSION:
            thermometer_on_msg_request_conversion();
            break;
        default:
            ESP_LOGE(TAG, "Unknown message: %d", (int)msg.id);
            break;
        }
    }
}

void thermometer_init() {
    thermometer_queue = xQueueCreate(2, sizeof(thermometer_message));
    assert(thermometer_queue != NULL);

    BaseType_t xRet = xTaskCreate(thermometer_task, "thermometer", 4096, NULL, 5, NULL);
    assert(xRet == pdPASS);
}

void thermometer_request_conversion() {
    thermometer_message msg{
        .id = THERMOMETER_MESSAGE_REQUEST_CONVERSION,
    };
    assert(xQueueSend(thermometer_queue, &msg, BLOCK_TIME) == pdTRUE);
}
