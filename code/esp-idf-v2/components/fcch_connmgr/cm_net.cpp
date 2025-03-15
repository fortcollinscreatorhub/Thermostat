// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <string.h>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include "fcch_connmgr/cm_conf.h"
#include "fcch_connmgr/cm_mqtt.h"
#include "fcch_connmgr/cm_net.h"
#include "fcch_connmgr/cm_util.h"
#include "cm_admin.h"
#include "cm_nvs.h"

static const char *TAG = "cm_net";

esp_netif_t *cm_net_if_ap;
esp_netif_t *cm_net_if_sta;

ESP_EVENT_DEFINE_BASE(CM_NET_EVENT);
enum cm_net_event {
    CM_NET_EVENT_TIMER_AP_ON,
    CM_NET_EVENT_TIMER_AP_OFF,
    CM_NET_EVENT_TIMER_STA_CONNECT,
};
enum cm_net_pending_action_ap {
    CM_NET_PENDING_ACTION_NONE,
    CM_NET_PENDING_ACTION_AP_ON,
    CM_NET_PENDING_ACTION_AP_OFF,
};
static enum cm_net_pending_action_ap cm_net_pending_action_ap =
    CM_NET_PENDING_ACTION_NONE;
static int cm_net_ap_sta_count = 0;
static bool cm_net_ap_on = true;
static bool cm_net_sta_connected = false;
static bool cm_net_sta_has_ip = false;
static bool cm_net_pending_action_sta_connect = false;
static bool cm_net_any_sta_defined = false;
static bool cm_net_restart_pending = false;
static size_t cm_net_sta_index;
static TimerHandle_t cm_net_timer_ap_on;
static TimerHandle_t cm_net_timer_ap_off;
static TimerHandle_t cm_net_timer_sta_connect;

static const char *cm_net_sta_net[2];
static const char *cm_net_sta_pass[2];
static cm_conf_item cm_net_item_sta_net1 = {
    .slug_name = "n1", // Network 1
    .text_name = "WiFi Network Name 1",
    .type = CM_CONF_ITEM_TYPE_STR,
    .p_val = {.str = &cm_net_sta_net[0] },
    .default_func = &cm_conf_default_str_empty,
};
static cm_conf_item cm_net_item_sta_pass1 = {
    .slug_name = "p1", // Password 1
    .text_name = "WiFi Password 1",
    .type = CM_CONF_ITEM_TYPE_PASS,
    .p_val = {.str = &cm_net_sta_pass[0] },
    .default_func = &cm_conf_default_str_empty,
};
static cm_conf_item cm_net_item_sta_net2 = {
    .slug_name = "n2", // Network 2
    .text_name = "WiFi Network Name 2",
    .type = CM_CONF_ITEM_TYPE_STR,
    .p_val = {.str = &cm_net_sta_net[1] },
    .default_func = &cm_conf_default_str_empty,
};
static cm_conf_item cm_net_item_sta_pass2 = {
    .slug_name = "p2", // Password 2
    .text_name = "WiFi Password 2",
    .type = CM_CONF_ITEM_TYPE_PASS,
    .p_val = {.str = &cm_net_sta_pass[1] },
    .default_func = &cm_conf_default_str_empty,
};

static void cm_net_replace_invalid_hostname(cm_conf_item *item, cm_conf_p_val p_val_u) {
    const char **p_val = p_val_u.str;
    const char *cur_val = *p_val;
    if ((cur_val != NULL) && (cur_val[0] != '\0')) {
        char *fixed = strdup(cur_val);
        char *d = fixed;
        const char *s = fixed;
        while (*s) {
            char c = *s++;
            bool legal =
                (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                (c == '.');
            if (!legal)
                c = '-';
            *d++ = c;
        }
        *d = '\0';
        free((void *)cur_val);
        *p_val = fixed;
        return;
    }
    uint8_t mac[6];
    esp_base_mac_addr_get(mac);
    char *val = NULL;
    asprintf(&val, "ESP-%02x%02x", (int)mac[4], (int)mac[5]);
    *p_val = val;
    assert(val != NULL);
}

const char *cm_net_hostname;
static cm_conf_item cm_net_item_hostname = {
    .slug_name = "hn", // HostName
    .text_name = "Hostname",
    .type = CM_CONF_ITEM_TYPE_STR,
    .p_val = { .str = &cm_net_hostname },
    .default_func = &cm_conf_default_str_empty,
    .replace_invalid_value = &cm_net_replace_invalid_hostname,
};

static cm_conf_item *cm_net_items[] = {
    &cm_net_item_sta_net1,
    &cm_net_item_sta_pass1,
    &cm_net_item_sta_net2,
    &cm_net_item_sta_pass2,
    &cm_net_item_hostname,
};

static cm_conf_page cm_net_page_wifi = {
    .slug_name = "w", // Wifi
    .text_name = "WiFi",
    .items = cm_net_items,
    .items_count = ARRAY_SIZE(cm_net_items),
};

void cm_net_register_conf() {
    cm_conf_register_page(&cm_net_page_wifi);
}

static void cm_net_on_timer_ap_on(TimerHandle_t xTimer) {
    ESP_ERROR_CHECK(esp_event_post(CM_NET_EVENT, CM_NET_EVENT_TIMER_AP_ON,
        NULL, 0, 10 / portTICK_PERIOD_MS));
}

static void cm_net_on_timer_ap_off(TimerHandle_t xTimer) {
    ESP_ERROR_CHECK(esp_event_post(CM_NET_EVENT, CM_NET_EVENT_TIMER_AP_OFF,
        NULL, 0, 10 / portTICK_PERIOD_MS));
}

static void cm_net_on_timer_sta_connect(TimerHandle_t xTimer) {
    ESP_ERROR_CHECK(esp_event_post(CM_NET_EVENT, CM_NET_EVENT_TIMER_STA_CONNECT,
        NULL, 0, 10 / portTICK_PERIOD_MS));
}

static void cm_net_maybe_start_pending_event_ap() {
    bool desired_ap_on =
        (!(cm_net_sta_connected && cm_net_sta_has_ip)) ||
        (cm_net_ap_sta_count != 0);

    if (cm_net_ap_on == desired_ap_on) {
        xTimerStop(cm_net_timer_ap_on, 10 / portTICK_PERIOD_MS);
        xTimerStop(cm_net_timer_ap_off, 10 / portTICK_PERIOD_MS);
        cm_net_pending_action_ap = CM_NET_PENDING_ACTION_NONE;
        return;
    }

    enum cm_net_pending_action_ap desired_pending_action =
        desired_ap_on ?
            CM_NET_PENDING_ACTION_AP_ON :
            CM_NET_PENDING_ACTION_AP_OFF;
    if (cm_net_pending_action_ap == desired_pending_action)
        return;
    if (desired_ap_on)
        xTimerStop(cm_net_timer_ap_off, 10 / portTICK_PERIOD_MS);
    else
        xTimerStop(cm_net_timer_ap_on, 10 / portTICK_PERIOD_MS);
    if (desired_ap_on)
        xTimerStart(cm_net_timer_ap_on, 10 / portTICK_PERIOD_MS);
    else
        xTimerStart(cm_net_timer_ap_off, 10 / portTICK_PERIOD_MS);
    cm_net_pending_action_ap = desired_pending_action;
}

static void cm_net_config_sta() {
    wifi_config_t wifi_config_sta{};
    wifi_config_sta.sta.pmf_cfg.capable = true;
    wifi_config_sta.sta.pmf_cfg.required = false;
    strcpy((char *)wifi_config_sta.sta.ssid, cm_net_sta_net[cm_net_sta_index]);
    strcpy((char *)wifi_config_sta.sta.password, cm_net_sta_pass[cm_net_sta_index]);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta));
}

static void cm_net_wifi_connect(bool first_time) {
    if (first_time)
        cm_net_sta_index = ARRAY_SIZE(cm_net_sta_net) - 1;
    while (true) {
        cm_net_sta_index++;
        if (cm_net_sta_index >= ARRAY_SIZE(cm_net_sta_net))
            cm_net_sta_index = 0;
        if (cm_net_sta_net[cm_net_sta_index][0])
            break;
    }
    cm_net_config_sta();
    esp_wifi_connect();
}

static void cm_net_maybe_start_pending_event_sta() {
    if (cm_net_restart_pending)
        return;
    if (!cm_net_any_sta_defined)
        return;
    if (cm_net_sta_connected)
        return;
    if (cm_net_pending_action_sta_connect)
        return;

    static bool cm_net_early_boot = true;
    TickType_t now = xTaskGetTickCount();
    uint64_t ms_since_boot = now * portTICK_PERIOD_MS;
    if (ms_since_boot > 10000)
        cm_net_early_boot = false;

    // Too-rapid STA reconnect attempts take radio time away from the AP.
    // If the AP isn't active, there's no need to rate-limit connect
    // attempts. Otherwise, we defer connect attempts using a timer.
    if (!cm_net_ap_on || cm_net_early_boot) {
        cm_net_wifi_connect(false);
        return;
    }

    cm_net_pending_action_sta_connect = true;
    xTimerStart(cm_net_timer_sta_connect, 10 / portTICK_PERIOD_MS);
}

static void cm_net_maybe_start_pending_event() {
    cm_net_maybe_start_pending_event_ap();
    cm_net_maybe_start_pending_event_sta();
}

static void cm_net_set_mode_ap_or_apsta() {
    wifi_mode_t wifi_mode = WIFI_MODE_AP;
    if (cm_net_any_sta_defined)
        wifi_mode = WIFI_MODE_APSTA;
    ESP_ERROR_CHECK(esp_wifi_set_mode(wifi_mode));
}

static void cm_net_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    if (event_base != CM_NET_EVENT) {
        ESP_LOGE(TAG, "cm_net_event_handler: event_base != CM_NET_EVENT");
        return;
    }

    switch (event_id) {
        case CM_NET_EVENT_TIMER_AP_ON:
            ESP_LOGI(TAG, "CM_NET_EVENT_TIMER_AP_ON");
            if (cm_net_pending_action_ap == CM_NET_PENDING_ACTION_AP_ON) {
                cm_net_ap_on = true;
                cm_net_set_mode_ap_or_apsta();
                cm_net_maybe_start_pending_event();
            }
            break;
        case CM_NET_EVENT_TIMER_AP_OFF:
            ESP_LOGI(TAG, "CM_NET_EVENT_TIMER_AP_OFF");
            if (cm_net_pending_action_ap == CM_NET_PENDING_ACTION_AP_OFF) {
                cm_net_ap_on = false;
                // Execution can only reach this point if STA is connected.
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                cm_net_maybe_start_pending_event();
            }
            break;
        case CM_NET_EVENT_TIMER_STA_CONNECT:
            ESP_LOGI(TAG, "CM_NET_EVENT_TIMER_STA_CONNECT");
            cm_net_pending_action_sta_connect = false;
            if (!cm_net_sta_connected)
                cm_net_wifi_connect(false);
            break;
        default:
            ESP_LOGI(TAG, "CM_NET_EVENT_? %" PRId32, event_id);
            break;
    }
}

static void cm_net_wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    if (event_base != WIFI_EVENT) {
        ESP_LOGE(TAG, "cm_net_wifi_event_handler: event_base != WIFI_EVENT");
        return;
    }
    switch (event_id) {
        case WIFI_EVENT_WIFI_READY:
            ESP_LOGI(TAG, "WIFI_READY");
            break;
        case WIFI_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "SCAN_DONE");
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP_START");
            cm_net_ap_sta_count = 0;
            break;
        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "AP_STOP");
            cm_net_ap_sta_count = 0;
            break;
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event =
                (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "AP_STACONNECTED: " MACSTR " join, AID=%d",
                MAC2STR(event->mac), event->aid);
            cm_net_ap_sta_count++;
            cm_net_maybe_start_pending_event();
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event =
                (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "AP_STADISCONNECTED: " MACSTR " leave, AID=%d",
                MAC2STR(event->mac), event->aid);
            if (cm_net_ap_sta_count) {
                cm_net_ap_sta_count--;
                cm_net_maybe_start_pending_event();
            }
            break;
        }
        case WIFI_EVENT_AP_PROBEREQRECVED:
            ESP_LOGI(TAG, "AP_PROBEREQRECVED");
            break;
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA_START");
            cm_net_wifi_connect(true);
            break;
        case WIFI_EVENT_STA_STOP:
            ESP_LOGI(TAG, "STA_STOP");
            break;
        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *event =
                (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "STA_CONNECTED: %s " MACSTR,
                event->ssid, MAC2STR(event->bssid));
            cm_net_sta_connected = true;
            cm_net_maybe_start_pending_event();
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "STA_DISCONNECTED");
            cm_net_sta_connected = false;
            cm_net_maybe_start_pending_event();
            break;
        case WIFI_EVENT_STA_AUTHMODE_CHANGE:
            ESP_LOGI(TAG, "STA_AUTHMODE_CHANGE");
            break;
        default:
            ESP_LOGI(TAG, "WIFI_EVENT_? %" PRId32, event_id);
            break;
    }
}

static void cm_net_ip_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    if (event_base != IP_EVENT) {
        ESP_LOGE(TAG, "cm_net_ip_event_handler: event_base != IP_EVENT");
        return;
    }
    switch (event_id) {
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t* event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "STA_GOT_IP: ip=" IPSTR, IP2STR(&event->ip_info.ip));
            cm_net_sta_has_ip = true;
            cm_net_maybe_start_pending_event();
            cm_mqtt_start();
            break;
        }
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGI(TAG, "STA_LOST_IP");
            cm_net_sta_has_ip = false;
            cm_net_maybe_start_pending_event();
            break;
        case IP_EVENT_AP_STAIPASSIGNED:
            ESP_LOGI(TAG, "AP_STAIPASSIGNED");
            break;
        case IP_EVENT_GOT_IP6:
            ESP_LOGI(TAG, "GOT_IP6");
            break;
        case IP_EVENT_ETH_GOT_IP:
            ESP_LOGI(TAG, "ETH_GOT_IP");
            break;
        case IP_EVENT_ETH_LOST_IP:
            ESP_LOGI(TAG, "ETH_LOST_IP");
            break;
        default: {
            ESP_LOGI(TAG, "IP_EVENT_? %" PRId32, event_id);
            break;
        }
    }
}

static void cm_net_config_ap() {
    wifi_config_t wifi_config{};
    strlcpy((char *)wifi_config.ap.ssid, cm_net_hostname,
        sizeof(wifi_config.ap.ssid));
    strlcpy((char *)wifi_config.ap.password, cm_admin_password,
        sizeof(wifi_config.ap.password));
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    if (wifi_config.ap.password[0] != '\0') {
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    wifi_config.ap.max_connection = 2;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
}

void cm_net_init() {
    for (size_t i = 0; i < ARRAY_SIZE(cm_net_sta_net); i++) {
        if (cm_net_sta_net[i][0]) {
            cm_net_any_sta_defined = true;
            break;
        }
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    cm_net_if_ap = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_netif_set_hostname(cm_net_if_ap, cm_net_hostname));
    cm_net_if_sta = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(cm_net_if_sta, cm_net_hostname));

    cm_net_timer_ap_on = xTimerCreate(
        "cm_net_ap_on",
        (120 * 1000) / portTICK_PERIOD_MS,
        pdFALSE,
        NULL,
        cm_net_on_timer_ap_on);
    cm_net_timer_ap_off = xTimerCreate(
        "cm_net_ap_off",
        (60 * 1000) / portTICK_PERIOD_MS,
        pdFALSE,
        NULL,
        cm_net_on_timer_ap_off);
    cm_net_timer_sta_connect = xTimerCreate(
        "cm_net_sta_connect",
        (10 * 1000) / portTICK_PERIOD_MS,
        pdFALSE,
        NULL,
        cm_net_on_timer_sta_connect);

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_restore());
    cm_net_set_mode_ap_or_apsta();
    cm_net_config_ap();
    ESP_ERROR_CHECK(esp_event_handler_register(CM_NET_EVENT, ESP_EVENT_ANY_ID,
        &cm_net_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        &cm_net_wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
        &cm_net_ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void cm_net_notify_upcoming_restart() {
    cm_net_restart_pending = true;
}

cm_net_ap_info cm_net_get_ap_info() {
    cm_net_ap_info info;

    info.enabled = cm_net_ap_on;
    info.network = cm_net_hostname;
    info.ip = 0;
    if (info.enabled) {
        esp_netif_ip_info_t ap_ip_info;
        esp_err_t err = esp_netif_get_ip_info(cm_net_if_ap, &ap_ip_info);
        if (err == ESP_OK) {
            info.ip = ap_ip_info.ip.addr;
        }
    }
    return info;
}

cm_net_sta_info cm_net_get_sta_info() {
    cm_net_sta_info info;

    info.connected = cm_net_sta_connected;
    info.has_ip = cm_net_sta_has_ip;
    info.network = cm_net_sta_net[cm_net_sta_index];
    info.ip = 0;
    if (info.has_ip) {
        esp_netif_ip_info_t sta_ip_info;
        esp_err_t err = esp_netif_get_ip_info(cm_net_if_sta, &sta_ip_info);
        if (err == ESP_OK)
            info.ip = sta_ip_info.ip.addr;
        if (info.ip == 0)
            info.has_ip = false;
    }
    return info;
}
