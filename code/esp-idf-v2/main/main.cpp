// Copyright 2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <fcch_connmgr/cm.h>
#include <fcch_connmgr/cm_conf.h>
#include <fcch_connmgr/cm_mqtt.h>
#include <fcch_connmgr/cm_util.h>

#include "buttons.h"
#include "lcd.h"
#include "mqtt.h"
#include "thermometer.h"

#define BLOCK_TIME (10 / portTICK_PERIOD_MS)
#define TICKS_PER_TEMP_REQUEST 5

#define MAIN_GPIO_RELAY GPIO_NUM_33
#define MAIN_GPIO_HEATING_LED GPIO_NUM_26
#define MAIN_GPIO_OVERRIDE_LED GPIO_NUM_25

enum main_message_id {
    MAIN_MESSAGE_TIMER,
    MAIN_MESSAGE_BUTTON,
    MAIN_MESSAGE_TEMP_F,
};

struct main_message {
    main_message_id id;
    union {
        buttons_id button_id;
        float temp_f;
        // For messages without any parameter:
        uint32_t none;
    };
};

static const char *TAG = "main";
static QueueHandle_t main_queue;
static TimerHandle_t main_1s_timer;
static uint32_t main_thermostat_errors;
static uint32_t main_ticks_since_temp_request = TICKS_PER_TEMP_REQUEST;
static uint16_t main_override_time_left;
static uint16_t main_mqtt_time_left;
static bool main_heating_on;
// For LCD & MQTT reporting:
static float main_last_temp_f;
static float main_target_temp_f;

static void main_item_val_default_f_1(cm_conf_item * /*item*/, cm_conf_p_val p_val_u) {
    *p_val_u.f = 1;
}

static void main_item_val_default_f_3(cm_conf_item * /*item*/, cm_conf_p_val p_val_u) {
    *p_val_u.f = 3;
}

static void main_item_val_default_f_50(cm_conf_item * /*item*/, cm_conf_p_val p_val_u) {
    *p_val_u.f = 50;
}

static void main_item_val_default_f_70(cm_conf_item * /*item*/, cm_conf_p_val p_val_u) {
    *p_val_u.f = 70;
}

static void main_item_val_default_u16_2h_in_s(cm_conf_item * /*item*/, cm_conf_p_val p_val_u) {
    *p_val_u.u16 = 2 * 60 * 60;
}

static float main_item_val_sensor_offset;
static cm_conf_item main_item_sensor_offset = {
    .slug_name = "so", // Sensor Offset
    .text_name = "Sensor Offset",
    .type = CM_CONF_ITEM_TYPE_FLOAT,
    .p_val = {.f = &main_item_val_sensor_offset },
    .default_func = &cm_conf_default_f_0,
};

static float main_item_val_fallback_target_temp_f;
static cm_conf_item main_item_fallback_target_temp_f = {
    .slug_name = "ftt", // Fallback Target Temp
    .text_name = "Fallback Target Temperature",
    .type = CM_CONF_ITEM_TYPE_FLOAT,
    .p_val = {.f = &main_item_val_fallback_target_temp_f },
    .default_func = &main_item_val_default_f_50,
};

static float main_item_val_fallback_hysteresis_low;
static cm_conf_item main_item_fallback_hysteresis_low = {
    .slug_name = "fhl", // Fallback Hysteresis Low
    .text_name = "Fallback Hysteresis Offset Low",
    .type = CM_CONF_ITEM_TYPE_FLOAT,
    .p_val = {.f = &main_item_val_fallback_hysteresis_low },
    .default_func = &main_item_val_default_f_3,
};

static float main_item_val_fallback_hysteresis_high;
static cm_conf_item main_item_fallback_hysteresis_high = {
    .slug_name = "fhh", // Fallback Hysteresis Offset High
    .text_name = "Fallback Hysteresis High",
    .type = CM_CONF_ITEM_TYPE_FLOAT,
    .p_val = {.f = &main_item_val_fallback_hysteresis_high },
    .default_func = &main_item_val_default_f_1,
};

static uint16_t main_item_val_override_duration;
static cm_conf_item main_item_override_duration = {
    .slug_name = "od", // Override Duration
    .text_name = "Override Duration (seconds)",
    .type = CM_CONF_ITEM_TYPE_U16,
    .p_val = {.u16 = &main_item_val_override_duration },
    .default_func = &main_item_val_default_u16_2h_in_s,
};

static float main_item_val_override_target_temp_f;
static cm_conf_item main_item_override_target_temp_f = {
    .slug_name = "ott", // Override Target Temp
    .text_name = "Override Target Temperature",
    .type = CM_CONF_ITEM_TYPE_FLOAT,
    .p_val = {.f = &main_item_val_override_target_temp_f },
    .default_func = &main_item_val_default_f_70,
};

static float main_item_val_override_hysteresis_low;
static cm_conf_item main_item_override_hysteresis_low = {
    .slug_name = "ohl", // Override Hysteresis Low
    .text_name = "Override Hysteresis Offset Low",
    .type = CM_CONF_ITEM_TYPE_FLOAT,
    .p_val = {.f = &main_item_val_override_hysteresis_low },
    .default_func = &main_item_val_default_f_3,
};

static float main_item_val_override_hysteresis_high;
static cm_conf_item main_item_override_hysteresis_high = {
    .slug_name = "ohh", // Override Hysteresis High
    .text_name = "Override Hysteresis Offset High",
    .type = CM_CONF_ITEM_TYPE_FLOAT,
    .p_val = {.f = &main_item_val_override_hysteresis_high },
    .default_func = &main_item_val_default_f_1,
};

static cm_conf_item *main_items[] = {
    &main_item_sensor_offset,
    &main_item_fallback_target_temp_f,
    &main_item_fallback_hysteresis_low,
    &main_item_fallback_hysteresis_high,
    &main_item_override_duration,
    &main_item_override_target_temp_f,
    &main_item_override_hysteresis_low,
    &main_item_override_hysteresis_high,
};

static cm_conf_page main_conf_page = {
    .slug_name = "ts", // ThermoStat
    .text_name = "Thermostat",
    .items = main_items,
    .items_count = ARRAY_SIZE(main_items),
};

lcd_thermostat_info lcd_get_thermostat_info() {
    return {main_last_temp_f, main_target_temp_f, main_override_time_left};
}

mqtt_status mqtt_get_status() {
    return {main_last_temp_f, main_target_temp_f, main_heating_on, main_override_time_left};
}

void buttons_on_click(buttons_id id) {
    main_message msg{
        .id = MAIN_MESSAGE_BUTTON,
        .button_id = id,
    };
    assert(xQueueSend(main_queue, &msg, BLOCK_TIME) == pdTRUE);
}

static void main_http_action_button_start() {
    buttons_on_click(BUTTONS_ID_START);
}

static const char *main_http_action_button_start_description() {
    return "Override Start";
}

static void main_http_action_button_cancel() {
    buttons_on_click(BUTTONS_ID_CANCEL);
}

static const char *main_http_action_button_cancel_description() {
    return "Override Cancel";
}

void thermometer_on_error() {
    main_thermostat_errors++;
    assert(main_thermostat_errors < 10);
}

void thermometer_on_temp_f(float temp_f) {
    main_thermostat_errors = 0;

    main_message msg{
        .id = MAIN_MESSAGE_TEMP_F,
        .temp_f = temp_f,
    };
    assert(xQueueSend(main_queue, &msg, BLOCK_TIME) == pdTRUE);
}

static void main_on_1s_timer(TimerHandle_t /*xTimer*/) {
    main_message msg{
        .id = MAIN_MESSAGE_TIMER,
        .none = 0,
    };
    assert(xQueueSend(main_queue, &msg, BLOCK_TIME) == pdTRUE);

    main_ticks_since_temp_request++;
    if (main_ticks_since_temp_request >= TICKS_PER_TEMP_REQUEST)
        thermometer_request_conversion();

    lcd_on_1s_timer();
}

static void main_hw_init() {
    gpio_num_t gpios[] = {
        MAIN_GPIO_RELAY,
        MAIN_GPIO_HEATING_LED,
        MAIN_GPIO_OVERRIDE_LED,
    };
    for (auto gpio : gpios) {
        ESP_ERROR_CHECK(gpio_reset_pin(gpio));
        ESP_ERROR_CHECK(gpio_set_direction(gpio, GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(gpio, 0));
    }
}

static void main_evaluate_heating() {
    uint16_t hysteresis_high;
    uint16_t hysteresis_low;
    if (main_override_time_left) {
        main_target_temp_f = main_item_val_override_target_temp_f;
        hysteresis_high = main_item_val_override_hysteresis_high;
        hysteresis_low = main_item_val_override_hysteresis_low;
    } else {
        main_target_temp_f = main_item_val_fallback_target_temp_f;
        hysteresis_high = main_item_val_fallback_hysteresis_high;
        hysteresis_low = main_item_val_fallback_hysteresis_low;
    }
    ESP_ERROR_CHECK(gpio_set_level(MAIN_GPIO_OVERRIDE_LED,
        main_override_time_left ? 1 : 0));

    bool new_main_heating_on = main_heating_on;
    if (main_heating_on) {
        if (main_last_temp_f >= main_target_temp_f + hysteresis_high)
            new_main_heating_on = false;
    } else {
        if (main_last_temp_f <= main_target_temp_f - hysteresis_low)
            new_main_heating_on = true;
    }

    main_heating_on = new_main_heating_on;
    ESP_ERROR_CHECK(gpio_set_level(MAIN_GPIO_RELAY, main_heating_on));
    ESP_ERROR_CHECK(gpio_set_level(MAIN_GPIO_HEATING_LED, main_heating_on));
}

static void main_on_msg_timer(const main_message &msg) {
    if (main_override_time_left > 0) {
        main_override_time_left--;
        main_evaluate_heating();
    }
    if (main_mqtt_time_left > 0) {
        main_mqtt_time_left--;
        if (main_mqtt_time_left == 0) {
            main_mqtt_time_left = cm_mqtt_status_period;
            mqtt_send_status();
        }
    }
}

static void main_on_msg_button(const main_message &msg) {
    switch (msg.button_id) {
    case BUTTONS_ID_START:
        main_override_time_left = main_item_val_override_duration;
        break;
    case BUTTONS_ID_CANCEL:
        main_override_time_left = 0;
        break;
    case BUTTONS_ID_SET:
    case BUTTONS_ID_UP:
    case BUTTONS_ID_DOWN:
        break;
    default:
        ESP_LOGE(TAG, "Unknown button_id: %d", (int)msg.button_id);
        break;
    }
    main_evaluate_heating();
}

static void main_on_msg_temp_f(const main_message &msg) {
    main_last_temp_f = msg.temp_f + main_item_val_sensor_offset;
    main_evaluate_heating();
    lcd_on_thermostat_change();
}

static void main_task(void *pvParameters) {
    for (;;) {
        main_message msg;
        assert(xQueueReceive(main_queue, &msg, portMAX_DELAY) == pdTRUE);
        ESP_LOGD(TAG, "msg.id %d", msg.id);
        switch (msg.id) {
        case MAIN_MESSAGE_TIMER:
            main_on_msg_timer(msg);
            break;
        case MAIN_MESSAGE_BUTTON:
            main_on_msg_button(msg);
            break;
        case MAIN_MESSAGE_TEMP_F:
            main_on_msg_temp_f(msg);
            break;
        default:
            ESP_LOGE(TAG, "Unknown message: %d", (int)msg.id);
            break;
        }
    }
}

static void main_task_init() {
    main_queue = xQueueCreate(8, sizeof(main_message));
    assert(main_queue != NULL);

    BaseType_t xRet = xTaskCreate(main_task, "main", 4096, NULL, 5, NULL);
    assert(xRet == pdPASS);
}

static void main_timer_init_start() {
    main_1s_timer = xTimerCreate(
        "main1s",
        1000 / portTICK_PERIOD_MS,
        pdTRUE,
        NULL,
        main_on_1s_timer);
    assert(main_1s_timer != NULL);
    assert(xTimerStart(main_1s_timer, BLOCK_TIME));
}

extern "C" void app_main(void) {
    cm_register_conf();
    cm_conf_register_page(&main_conf_page);
    cm_init();
    main_hw_init();
    main_task_init();
    lcd_init();
    buttons_init();
    cm_http_register_home_action(
        "button-start",
        main_http_action_button_start_description,
        main_http_action_button_start
    );
    cm_http_register_home_action(
        "button-cancel",
        main_http_action_button_cancel_description,
        main_http_action_button_cancel
    );
    thermometer_init();
    if (cm_mqtt_status_period) {
        mqtt_init();
        main_mqtt_time_left = 10; // delay a bit initially
    }
    main_timer_init_start();
}
