// Copyright 2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <driver/gpio.h>
#include <stdio.h>
#include <esp_log.h>
#include <iot_button.h>
#include <button_gpio.h>
#include "buttons.h"

#define BUTTONS_GPIO_START GPIO_NUM_23 // SWITCH0 in schematic
#define BUTTONS_GPIO_CANCEL GPIO_NUM_18 // SWITCH1 in schematic
#define BUTTONS_GPIO_SET GPIO_NUM_19
#define BUTTONS_GPIO_UP GPIO_NUM_17
#define BUTTONS_GPIO_DOWN GPIO_NUM_16

//static const char *TAG = "buttons";

static void buttons_event_cb(void *arg, void *data) {
    buttons_id id = (buttons_id)(uint32_t)data;
    buttons_on_click(id);
}

static void buttons_register(
    int32_t gpio,
    buttons_id id
) {
    button_config_t btn_cfg = {
        .long_press_time = 0,
        .short_press_time = 0,
    };
    button_gpio_config_t gpio_cfg = {
        .gpio_num = gpio,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = true,
    };

    button_handle_t btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn));
    ESP_ERROR_CHECK(iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, buttons_event_cb, (void *)id));
}

void buttons_init() {
    buttons_register(BUTTONS_GPIO_START, BUTTONS_ID_START);
    buttons_register(BUTTONS_GPIO_CANCEL, BUTTONS_ID_CANCEL);
    buttons_register(BUTTONS_GPIO_SET, BUTTONS_ID_SET);
    buttons_register(BUTTONS_GPIO_UP, BUTTONS_ID_UP);
    buttons_register(BUTTONS_GPIO_DOWN, BUTTONS_ID_DOWN);
}
