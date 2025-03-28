// Copyright 2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <i2c-lcd1602.h>
#include <smbus.h>
#include <string.h>

#include "fcch_connmgr/cm.h"
#include "fcch_connmgr/cm_conf.h"
#include "fcch_connmgr/cm_net.h"
#include "fcch_connmgr/cm_mqtt.h"
#include "fcch_connmgr/cm_util.h"
#include "lcd.h"
#include "whole_tenths.h"

#define LCD_I2C_MASTER_NUM        I2C_NUM_0
#define LCD_I2C_MASTER_TX_BUF_LEN 0 // disabled
#define LCD_I2C_MASTER_RX_BUF_LEN 0 // disabled
#define LCD_I2C_MASTER_FREQ_HZ    100000
#define LCD_I2C_MASTER_SDA_IO     GPIO_NUM_21
#define LCD_I2C_MASTER_SCL_IO     GPIO_NUM_22
#define LCD_I2C_ADDRESS           0x27
#define LCD_NUM_ROWS              2
#define LCD_NUM_COLUMNS           32
#define LCD_NUM_VISIBLE_COLUMNS   16

#define LCD_IP_STR_BUF_SIZE 16 // nnn.nnn.nnn.nnn plus NUL

#define U32IP2STR(ip) \
    uint32_t(((ip) >>  0) & 0xff), \
    uint32_t(((ip) >>  8) & 0xff), \
    uint32_t(((ip) >> 16) & 0xff), \
    uint32_t(((ip) >> 24) & 0xff)

typedef void lcd_page_draw_t(void);

struct lcd_page_conf_t {
    int display_ticks;
    lcd_page_draw_t *draw_page;
};

static lcd_page_draw_t lcd_draw_page_ident;
static lcd_page_draw_t lcd_draw_page_ap;
static lcd_page_draw_t lcd_draw_page_sta;
static lcd_page_draw_t lcd_draw_page_mqtt;
static lcd_page_draw_t lcd_draw_page_thermostat;

//static const char *TAG = "lcd";
static i2c_lcd1602_info_t *lcd_dev;
static lcd_page_conf_t lcd_pages[] = {
    {
        .display_ticks = 1,
        .draw_page = lcd_draw_page_ident,
    },
    {
        .display_ticks = 1,
        .draw_page = lcd_draw_page_ap,
    },
    {
        .display_ticks = 1,
        .draw_page = lcd_draw_page_sta,
    },
    {
        .display_ticks = 1,
        .draw_page = lcd_draw_page_mqtt,
    },
    {
        .display_ticks = 10,
        .draw_page = lcd_draw_page_thermostat,
    },
};
static int lcd_page;
static int lcd_page_ticks_left;
static char lcd_content[LCD_NUM_ROWS][LCD_NUM_VISIBLE_COLUMNS + 1];

static void lcd_write_content() {
    for (int row = 0; row < LCD_NUM_ROWS; row++) {
        char *p = &lcd_content[row][0];
        char *pe = &lcd_content[row][LCD_NUM_VISIBLE_COLUMNS];
        p += strnlen(p, LCD_NUM_VISIBLE_COLUMNS);
        while (p < pe)
            *p++ = ' ';
        *pe = '\0';
        ESP_ERROR_CHECK(i2c_lcd1602_move_cursor(lcd_dev, 0, row));
        ESP_ERROR_CHECK(i2c_lcd1602_write_string(lcd_dev, &lcd_content[row][0]));
    }
}

struct Temp_F {
    explicit Temp_F(float f) : f(f) { }
    float f;
};

struct Hours_Minutes {
    explicit Hours_Minutes(uint32_t seconds) {
        minutes = (seconds + 59) / 60;
        hours = minutes / 60;
        minutes = minutes % 60;
    }

    uint32_t hours;
    uint32_t minutes;
};

struct Seconds {
    explicit Seconds(uint32_t s) : s(s) { }
    uint32_t s;
};

class LineWriter {
public:
    LineWriter(int row) :
        p_buffer(&lcd_content[row][0]),
        p_write(p_buffer),
        len_left(LCD_NUM_VISIBLE_COLUMNS)
    {
        *p_write = '\0';
    }

    LineWriter& operator<<(const char* src) {
        size_t src_len = strlen(src);
        size_t copy_len = std::min(src_len, len_left);
        strncpy(p_write, src, copy_len + 1); // +1 for NUL byte
        p_write += copy_len;
        len_left -= copy_len;
        if (copy_len < src_len) {
            p_write[-1] = '.';
            p_write[-2] = '.';
        }
        return *this;
    }

    LineWriter& operator<<(Temp_F temp_f) {
        float f = temp_f.f;

        char buf[32];
        if (f < 0) {
            *this << "-";
            f = -f;
        }
        if (f >= 100) {
            snprintf(buf, sizeof(buf), "%lu", (uint32_t)f);
            *this << buf;
        } else {
            Whole_Tenths wt(f);
            snprintf(buf, sizeof(buf), "%lu.%lu", wt.whole, wt.tenths);
            *this << buf;
        }
        *this << "F";
        return *this;
    }

    LineWriter& operator<<(Seconds s) {
        Hours_Minutes hm(s.s);
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu:%02lu", hm.hours, hm.minutes);
        *this << buf;
        return *this;
    }

private:
    char *p_buffer;
    char *p_write;
    size_t len_left;
};

static void lcd_draw_page_ident() {
    bool is_protected = cm_admin_is_protected();

    LineWriter row0(0);
    row0 << "ID ";
    row0 << cm_net_hostname;

    LineWriter row1(1);
    if (is_protected)
        row1 << "admin protected";
    else
        row1 << "admin open";
}

static void lcd_draw_page_ap() {
    auto ap_info = cm_net_get_ap_info();

    LineWriter row0(0);
    row0 << "AP ";
    if (ap_info.enabled)
        row0 << ap_info.network;
    else
        row0 << "-";

    LineWriter row1(1);
    if (ap_info.enabled) {
        char buf[LCD_IP_STR_BUF_SIZE];
        snprintf(buf, sizeof(buf), "%lu.%lu.%lu.%lu", U32IP2STR(ap_info.ip));
        row1 << buf;
    }
}

static void lcd_draw_page_sta() {
    auto sta_info = cm_net_get_sta_info();

    LineWriter row0(0);
    row0 << "ST ";
    if (sta_info.connected)
        row0 << sta_info.network;
    else
        row0 << "-";

    LineWriter row1(1);
    if (sta_info.connected) {
        char buf[LCD_IP_STR_BUF_SIZE];
        snprintf(buf, sizeof(buf), "%lu.%lu.%lu.%lu", U32IP2STR(sta_info.ip));
        row1 << buf;
    }
}

static void lcd_draw_page_mqtt() {
    auto mqtt_info = cm_mqtt_get_info();

    LineWriter row0(0);
    row0 << "MQ ";
    if (mqtt_info.connected)
        row0 << cm_mqtt_client_name;
    else
        row0 << "-";

    LineWriter row1(1);
}

static void lcd_draw_page_thermostat() {
    auto thermostat_info = lcd_get_thermostat_info();

    LineWriter row0(0);
    row0 << Temp_F(thermostat_info.cur_temp_f);
    row0 << " tgt:";
    row0 << Temp_F(thermostat_info.target_temp_f);

    LineWriter row1(1);
    if (thermostat_info.override_time_s) {
        row1 << "Override ";
        row1 << Seconds(thermostat_info.override_time_s);
    } else {
        row1 << "Fallback";
    }
}

static void lcd_redraw_page() {
    lcd_pages[lcd_page].draw_page();
    lcd_write_content();
}

static void lcd_page_next() {
    lcd_page++;
    if (lcd_page >= ARRAY_SIZE(lcd_pages))
        lcd_page = 0;

    lcd_page_ticks_left = lcd_pages[lcd_page].display_ticks;
    lcd_pages[lcd_page].draw_page();
    lcd_redraw_page();
}

static void lcd_init_hw() {
    i2c_config_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = LCD_I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    conf.scl_io_num = LCD_I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
    conf.master.clk_speed = LCD_I2C_MASTER_FREQ_HZ;
    conf.clk_flags = 0;
    ESP_ERROR_CHECK(i2c_param_config(LCD_I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(LCD_I2C_MASTER_NUM, conf.mode,
        LCD_I2C_MASTER_RX_BUF_LEN, LCD_I2C_MASTER_TX_BUF_LEN, 0));

    smbus_info_t *smbus_info = smbus_malloc();
    ESP_ERROR_CHECK(smbus_init(smbus_info, LCD_I2C_MASTER_NUM, LCD_I2C_ADDRESS));
    ESP_ERROR_CHECK(smbus_set_timeout(smbus_info, 1000 / portTICK_PERIOD_MS));

    lcd_dev = i2c_lcd1602_malloc();
    ESP_ERROR_CHECK(i2c_lcd1602_init(lcd_dev, smbus_info, true,
        LCD_NUM_ROWS, LCD_NUM_COLUMNS, LCD_NUM_VISIBLE_COLUMNS));
    ESP_ERROR_CHECK(i2c_lcd1602_reset(lcd_dev));
    ESP_ERROR_CHECK(i2c_lcd1602_home(lcd_dev));
}

void lcd_init() {
    lcd_init_hw();

    lcd_page = -1;
    lcd_page_next();
}

void lcd_on_1s_timer() {
    if (lcd_page_ticks_left)
        lcd_page_ticks_left--;
    if (lcd_page_ticks_left <= 0)
        lcd_page_next();
}

void lcd_on_thermostat_change() {
    // FIXME: This should probably just check:
    // if (!lcd_pages[lcd_page].want_redraw(some_enum_indicating_data_that_changed))
    if (lcd_pages[lcd_page].draw_page != lcd_draw_page_thermostat)
        return;

    lcd_redraw_page();
}
