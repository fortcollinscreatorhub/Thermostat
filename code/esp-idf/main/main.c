#define _OPEN_SYS_ITOA_EXT
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "rom/uart.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "rom/uart.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"

#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "wificonfig.h"


#include "smbus.h"
#include "i2c-lcd1602.h"



#include "ds18b20.h"
#include "button.h"

// remove define below to disable wifi and MQTT
#define WIFI_ENABLED 1

// LCD1602
#define LCD_NUM_ROWS               2
#define LCD_NUM_COLUMNS            32
#define LCD_NUM_VISIBLE_COLUMNS    16

// LCD2004
//#define LCD_NUM_ROWS               4
//#define LCD_NUM_COLUMNS            40
//#define LCD_NUM_VISIBLE_COLUMNS    20

// Undefine USE_STDIN if no stdin is available (e.g. no USB UART) - a fixed delay will occur instead of a wait for a keypress.
#define USE_STDIN  1
//#undef USE_STDIN

#define I2C_MASTER_NUM           I2C_NUM_0
#define I2C_MASTER_TX_BUF_LEN    0                     // disabled
#define I2C_MASTER_RX_BUF_LEN    0                     // disabled
#define I2C_MASTER_FREQ_HZ       100000
#define I2C_MASTER_SDA_IO        21  //CONFIG_I2C_MASTER_SDA
#define I2C_MASTER_SCL_IO        22 //CONFIG_I2C_MASTER_SCL
#define LCD1602_I2C_ADDRESS      0x27  //LCD I2C Address

// Temp Sensors are on GPIO27
#define TEMP_BUS 27
#define LED 2
#define HIGH 1
#define LOW 0
#define digitalWrite gpio_set_level

/* The event group allows multiple bits for each event,
   but we only care about two events: 
   1. are we connected to to the AP with an IP?
   2. are we connected to to the MQTT broker?
*/
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int MQTT_CONNECTED_BIT = BIT1;


#define BUF_SIZE (1024)



#define LOOP_DELAY 10           // 10 milliseconds
#define FRAME_LIMIT 200         // 2 seconds
#define SHORT_DEBOUNCE 50L      // 50 milliseconds
#define MEDIUM_DEBOUNCE 200L    // 1/5 second
#define LONG_DEBOUNCE 2000L     // 2 seconds
#define SET_MODE_TIMEOUT 30000L

#define MAX_SETBACK_TEMP 65
#define MIN_SETBACK_TEMP 45
#define MAX_OVERRIDE_TEMP 74
#define MIN_OVERRIDE_TEMP 45
#define MAX_OVERRIDE_TIME 240
#define MIN_OVERRIDE_TIME 5
#define MAX_ANTICIPATION 5.0
#define MIN_ANTICIPATION 0.0
#define MAX_CYCLES_PER_HOUR 10
#define MIN_CYCLES_PER_HOUR 0 // Off
#define MAX_TEMP_OFFSET 10.0
#define MIN_TEMP_OFFSET 0

//////////////////////////////////////////////////////////////////////////////////////////
// No user-servicable parts below
//
#define FRAME_HALF_LIMIT (FRAME_LIMIT/2)

#define TEMP_PIN 27
#define RELAY_PIN 33
#define HEATING_LED_PIN 26
#define OVERRIDE_LED_PIN 25

#define SW_SET_PIN 19
#define SW_UP_PIN 17
#define SW_DOWN_PIN 16
#define SW_START_PIN 23
#define SW_CANCEL_PIN 18

#define SETBACK_TEMP_ADDR     0x0
#define OVERRIDE_TEMP_ADDR    (SETBACK_TEMP_ADDR     + sizeof(int))
#define OVERRIDE_MINUTES_ADDR (OVERRIDE_TEMP_ADDR    + sizeof(int))
#define CYCLE_LIMIT_ADDR      (OVERRIDE_MINUTES_ADDR + sizeof(int))
#define CYCLES_PER_HOUR_ADDR  (CYCLE_LIMIT_ADDR      + sizeof(bool))
#define ANTICIPATION_ADDR     (CYCLES_PER_HOUR_ADDR  + sizeof(int))
#define TEMPOFFSET_ADDR       (ANTICIPATION_ADDR     + sizeof(int))

float curr_temp = 70.0;                   // current temperature
int curr_set_point = 55;                 // setpoint temperature based on override or not
long override_start_time = 0;   // time (in millis) since override button was pressed
long override_finish_time = 0;  // time (in millis) when override will finish
bool heating = false;                    // are we currently heating?
long last_cycle_time = 0;       // time (in millis) since last heat cycle started
int frame = 0 ;                          // current display frame
long timeout_time = 0;          // time any of set/up/down was pressed + timeout period
long time_left = 0;             //time left for heating override 


// the following are restored from EEPROM at powerup
int setback_temp;     // setback or default temperature
int override_temp;    // override temperature
int override_minutes; // number of minutes an override will last
bool cycle_limit;     // enable cycles per hour limit
int cycles_per_hour;  // max number of cycles per hour
float anticipation;   // hysteresis degrees
float temp_offset;    // temperature sensor offset
QueueHandle_t button_events;

enum TempMode {
  Setback,
  Override
} temp_mode;



// NVS handler
nvs_handle my_handle;

QueueHandle_t * button_init(unsigned long long );
button_event_t ev;

i2c_lcd1602_info_t * lcd_info;


#define I2C_MASTER_NUM           I2C_NUM_0
#define I2C_MASTER_TX_BUF_LEN    0                     // disabled
#define I2C_MASTER_RX_BUF_LEN    0                     // disabled
#define I2C_MASTER_FREQ_HZ       100000
#define I2C_MASTER_SDA_IO        21  //CONFIG_I2C_MASTER_SDA
#define I2C_MASTER_SCL_IO        22 //CONFIG_I2C_MASTER_SCL
#define LCD1602_I2C_ADDRESS      0x27

  char*  str_setback="SETBACK";
  char*  str_override="OVERRIDE";
  char*  str_min_override = "MIN_OVERRIDE";
  char*  str_cycle_limit = "CYCLE_LIMIT";
  char*  str_cycles_per_hour = "CYCLES_PER_HOUR";
  char*  str_anticipation = "ANTICIPATION";
  char*  str_temp_offset = " TEMP_OFFSET";


// list of access points to try
//

struct ap_entry {
    char *ssid;
    char *password;
};
struct ap_entry *ap_list = NULL;
int ap_count = 0;
int ap_idx = 0;

// load AP credentials into list from wifi configuration structure
static void load_aps (void) {
    // first, find number of APs
    if (strlen(wificonfig_vals_wifi.ap1_ssid) > 0) ap_count++;
    if (strlen(wificonfig_vals_wifi.ap2_ssid) > 0) ap_count++;
    if (strlen(wificonfig_vals_wifi.ap3_ssid) > 0) ap_count++;
    if (strlen(wificonfig_vals_wifi.ap4_ssid) > 0) ap_count++;

    if (ap_count > 0)
        ap_list = malloc (sizeof(struct ap_entry) * ap_count);

    ap_idx = 0;
    if (strlen(wificonfig_vals_wifi.ap1_ssid) > 0) {
       ap_list[ap_idx].ssid = wificonfig_vals_wifi.ap1_ssid;
       ap_list[ap_idx++].password = wificonfig_vals_wifi.ap1_pswd;
    }
    if (strlen(wificonfig_vals_wifi.ap2_ssid) > 0) {
       ap_list[ap_idx].ssid = wificonfig_vals_wifi.ap2_ssid;
       ap_list[ap_idx++].password = wificonfig_vals_wifi.ap2_pswd;
    }
    if (strlen(wificonfig_vals_wifi.ap3_ssid) > 0) {
       ap_list[ap_idx].ssid = wificonfig_vals_wifi.ap3_ssid;
       ap_list[ap_idx++].password = wificonfig_vals_wifi.ap3_pswd;
    }
    if (strlen(wificonfig_vals_wifi.ap4_ssid) > 0) {
       ap_list[ap_idx].ssid = wificonfig_vals_wifi.ap4_ssid;
       ap_list[ap_idx++].password = wificonfig_vals_wifi.ap4_pswd;
    }
}

const char *TAG = "TEMP_Client";



static int access_state = false;

static void next_wifi()
{
    ESP_LOGI(TAG, "Disconnecting WiFi to change to next station");
    ESP_ERROR_CHECK( esp_wifi_disconnect() );
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "event_handler: Event dispatched from event loop base=%s, event_id=%d", event_base, event_id);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, wificonfig_vals_wifi.hostname);
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Got disconnected");
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
     
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

        // connect to next AP on list
        ap_idx++;
        if (ap_idx >= ap_count) {
            ap_idx = 0;
        }
        wifi_config_t wifi_config = {
            .sta = {
                .ssid = "",
                .password = "",
            },
        };
        strncpy((char *)wifi_config.sta.ssid, ap_list[ap_idx].ssid, 32);
        strncpy((char *)wifi_config.sta.password, ap_list[ap_idx].password, 64);

        ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
        ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
        ESP_ERROR_CHECK( esp_wifi_connect() );
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
       // gpio_set_level(GPIO_OUTPUT_CONNECTED_LED, 1);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Reverses a string 'str' of length 'len'
void reverse(char* str, int len)
{
    int i = 0, j = len - 1, temp;
    while (i < j) {
        temp = str[i];
        str[i] = str[j];
        str[j] = temp;
        i++;
        j--;
    }
}
 
// Converts a given integer x to string str[].
// d is the number of digits required in the output.
// If d is more than the number of digits in x,
// then 0s are added at the beginning.
int intToStr(int x, char str[], int d)
{
    int i = 0;
    while (x) {
        str[i++] = (x % 10) + '0';
        x = x / 10;
    }
 
    // If number of digits required is more, then
    // add 0s at the beginning
    while (i < d)
        str[i++] = '0';
 
    reverse(str, i);
    str[i] = '\0';
    return i;
}
 
// Converts a floating-point/double number to a string.
void ftoa(float n, char* res, int afterpoint)
{
    // Extract integer part
    int ipart = (int)n;
 
    // Extract floating part
    float fpart = n - (float)ipart;
 
    // convert integer part to string
    int i = intToStr(ipart, res, 0);
 
    // check for display option after point
    if (afterpoint != 0) {
        res[i] = '.'; // add dot
 
        // Get the value of fraction part upto given no.
        // of points after dot. The third parameter
        // is needed to handle cases like 233.007
        fpart = fpart * pow(10, afterpoint);
 
        intToStr((int)fpart, res + i + 1, afterpoint);
    }
}

static void initialize_wifi (void) {
    esp_netif_init();
    wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK( esp_wifi_set_ps(WIFI_PS_NONE) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
        },
    };

    ap_idx = 0;
    strncpy((char *)wifi_config.sta.ssid, (char *)ap_list[ap_idx].ssid, 32);
    strncpy((char *)wifi_config.sta.password, (char *)ap_list[ap_idx].password, 64);

    ESP_LOGI(TAG, "Setting WiFi configuration SSID '%s'...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static esp_mqtt_client_handle_t mqtt_client = NULL;
static int mqtt_connected = false;

static void publish_status (char *subtopic, char *value) {
   
    if ((mqtt_client == NULL) || !mqtt_connected) {
       return;
    }
    char topic[128];
    

    sprintf (topic, "stat/%s/%s", wificonfig_vals_mqtt.topic, subtopic);

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, value, 0, 1, 0);
    ESP_LOGI(TAG, "publish successful, msg_id=%d", msg_id);
}



static void mqtt_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    ESP_LOGI(TAG, "mqtt_event_handler: Event dispatched from event loop base=%s, event_id=%d", event_base, event_id);

    char full_topic[128];
    int msg_id;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;

    switch (event_id) {
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;

        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            mqtt_connected = true;
            sprintf (full_topic, "cmnd/%s/POWER", wificonfig_vals_mqtt.topic);
            msg_id = esp_mqtt_client_subscribe (mqtt_client, full_topic, 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            xEventGroupSetBits(wifi_event_group, MQTT_CONNECTED_BIT);
            // subscriptions would go here, but not needed for RFID reader
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_connected = false;
            xEventGroupClearBits(wifi_event_group, MQTT_CONNECTED_BIT);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
            if (event->data_len > 0) {
                if (strncmp (event->data, "ON", event->data_len) == 0) {
                    temp_mode = Override;
                } else {
                    temp_mode= Setback;
                }
            }
           
            break;

        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event_id);
            break;
    }
}

static void initialize_mqtt () {

    char uri[128];

    mqtt_client = NULL;
     mqtt_connected = false;
    xEventGroupClearBits(wifi_event_group, MQTT_CONNECTED_BIT);
    if (strcmp (wificonfig_vals_mqtt.host, "") == 0) {
        return;
    }

    sprintf (uri, "mqtt://%s", wificonfig_vals_mqtt.host);

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = uri,
        .port = wificonfig_vals_mqtt.port,
        .client_id = wificonfig_vals_mqtt.client,
        .username = wificonfig_vals_mqtt.user,
        .password = wificonfig_vals_mqtt.pswd,
    };

    // wait for Wifi connection
    while ((xEventGroupGetBits (wifi_event_group) & WIFI_CONNECTED_BIT) == 0) {
        vTaskDelay(100 / portTICK_RATE_MS);
    }


    mqtt_client = esp_mqtt_client_init (&mqtt_cfg);
    if (mqtt_client != NULL) {
        ESP_ERROR_CHECK(esp_mqtt_client_register_event (mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client));
        ESP_ERROR_CHECK(esp_mqtt_client_start (mqtt_client));
    } else {
        ESP_LOGE(TAG, "initialize_mqtt: Unable to initialize MQTT client");
    }
}

static void check_set (void *pvParameters) {
    int last_level = 1;
    int64_t pressed_time = 0;
    while (1) {
        int new_level = gpio_get_level(SW_SET_PIN);
        if (new_level != last_level) {
            int64_t curr_time = esp_timer_get_time ();
            if (new_level == 1) {
                int64_t curr_time = esp_timer_get_time ();
                if ((curr_time - pressed_time) > 3000000) {
                    trigger_wificonfig();
                } else {
                    next_wifi();
                }
            } else {
                pressed_time = curr_time;
            }
        }
        last_level = new_level;
        vTaskDelay(100 / portTICK_RATE_MS);
    }
}



// Return a single newline-terminated line from a socket
//
char recv_buf[256];
char *buf_pos;
int recv_len;
int recv_done;
void read_line_socket_init () {
    bzero(recv_buf, sizeof(recv_buf));
    buf_pos = recv_buf;
    recv_len = 0;
    recv_done = 0;
}
int read_line_socket (char *line, int s) {
    int r;
    char c;
    char *start_line = line;
    ESP_LOGV(TAG, "read_line_socket: <enter>");
    if (recv_done) {
        ESP_LOGV(TAG, "read_line_socket: recv_done");
        return (0);
    }
    while (1) {
        if (recv_len == 0) {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            if (r > 256) {
                ESP_LOGE(TAG, "read_line_socket: recv_buf overflow!");
                return (-1);
            }
            if (r < 0) {
                ESP_LOGV(TAG, "read_line_socket: recv_len==0, r<0, return 0, line = '%s'", start_line);
                return (0);
            }
            if (r == 0) {
                *line = '\0';
                recv_done = 1;
                ESP_LOGV(TAG, "read_line_socket: recv_len==0, r==0, recv_done, line = '%s'", start_line);
                return (buf_pos != recv_buf);
            }
            recv_len = r;
            buf_pos = recv_buf;
        }
        c = *buf_pos;
        buf_pos++;
        recv_len--;
        if (c == '\n') {
            *line = '\0';
            ESP_LOGV(TAG, "read_line_socket: line = '%s'", start_line);
            return (1);
        } else if (c != '\r') {
            *line = c;
            line++;
        }
    }
}


int http_code;
int http_code_done;
int http_header_done;

int open_server (int *s, char *path)  {
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;

    ESP_LOGV(TAG, "open_server: path = '%s'", path);

    http_code = 0;
    http_code_done = false;
    http_header_done = false;
    read_line_socket_init();

    if ((xEventGroupGetBits (wifi_event_group) & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGI(TAG, "Not Connected\n");
        return (-1);
    }
    
    char port_str[80];
    sprintf (port_str, "%u", wificonfig_vals_rfid.port);
    int err = getaddrinfo(wificonfig_vals_rfid.host, port_str, &hints, &res);

    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
        if (res != NULL)
            freeaddrinfo(res);
        return (-1);
    }

    /* Code to print the resolved IP.
    Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGD(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    *s = socket(res->ai_family, res->ai_socktype, 0);
    if(*s < 0) {
        ESP_LOGE(TAG, "... Failed to allocate socket.");
        freeaddrinfo(res);
        return (-1);
    }
    ESP_LOGD(TAG, "... allocated socket");

    if(connect(*s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
        close(*s);
        freeaddrinfo(res);
        return (-1);
    }

    ESP_LOGD(TAG, "... connected");
    freeaddrinfo(res);

    char request[256];
    sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: esp-idf/1.0 esp32\r\n\r\n", path, wificonfig_vals_rfid.host);
    ESP_LOGV(TAG, "request = '%s'", request);

    if (write(*s, request, strlen(request)) < 0) {
        ESP_LOGE(TAG, "... socket send failed");
        close(*s);
        return (-1);
    }
    ESP_LOGD(TAG, "... socket send success");

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(*s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
            sizeof(receiving_timeout)) < 0) {
        ESP_LOGE(TAG, "... failed to set socket receiving timeout");
        close(*s);
        return (-1);
    }
    ESP_LOGD(TAG, "... set socket receiving timeout success");

    return (0);
}

// Read one line of the body of a response from the server
// return value:  1 for sucessful read
//                0 for done reading
//               -1 for error
//
int read_server (char *body, int s) {

    /* Read HTTP response */
    body[0] = '\0';
    char line[256];
    int r;
    bzero(line, sizeof(line));
    while ((r = read_line_socket (line, s)) > 0) {
        ESP_LOGV(TAG, "Socket data = '%s'", line);
        if (strlen(line) == 0) {
            http_header_done = true;
        } else if (http_header_done) {
            strcpy(body, line);
            return (1);
        } else if (!http_code_done) {
            sscanf (line, "%*s %d %*s", &http_code);
            http_code_done = true;
            if (http_code != 200) {
               ESP_LOGE(TAG, "Bad HTTP return code (%d)", http_code);
            }
        }
    }
    if (r < 0) {
        ESP_LOGE(TAG, "...socket data not available");
        return (-1);
    }

    ESP_LOGI(TAG, "Done reading");
    return (0);
}

// send periodic updates
//
static void update_loop (void *pvParameters) {
    char value[128];

     while (1) {
        if (wificonfig_vals_mqtt.update != 0) {
            ftoa(curr_temp, value, 1);
            publish_status ("TEMPERATURE", value);
            sprintf(value,"%d",heating);
            publish_status ("HEATING",value );
            sprintf(value,"%d",((int) (time_left / 60000L)) );
            publish_status("OVERRIDE_MIN", value );
            vTaskDelay((60000 * wificonfig_vals_mqtt.update) / portTICK_RATE_MS);
        } else {
            // paranoia (should never get here)
            vTaskDelay(60000 / portTICK_RATE_MS);
        }
    }
}


long millis() {
  long timer;

  timer = esp_timer_get_time()/1000;

  return timer;

}

static void i2c_master_init(void)
{
    int i2c_master_port = I2C_NUM_0;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;  // GY-2561 provides 10kΩ pullups
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;  // GY-2561 provides 10kΩ pullups
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    conf.clk_flags = 0;
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       I2C_MASTER_RX_BUF_LEN,
                       I2C_MASTER_TX_BUF_LEN, 0);
}

void lcd1602_task(void * pvParameter)
{
    // Set up I2C
    i2c_master_init();
    i2c_port_t i2c_num = I2C_MASTER_NUM;
    uint8_t address = LCD1602_I2C_ADDRESS;

    // Set up the SMBus
    smbus_info_t * smbus_info = smbus_malloc();
    ESP_ERROR_CHECK(smbus_init(smbus_info, i2c_num, address));
    ESP_ERROR_CHECK(smbus_set_timeout(smbus_info, 1000 / portTICK_RATE_MS));

    // Set up the LCD1602 device with backlight off
    i2c_lcd1602_info_t * lcd_info = i2c_lcd1602_malloc();
    ESP_ERROR_CHECK(i2c_lcd1602_init(lcd_info, smbus_info, true,
                                     LCD_NUM_ROWS, LCD_NUM_COLUMNS, LCD_NUM_VISIBLE_COLUMNS));

    ESP_ERROR_CHECK(i2c_lcd1602_reset(lcd_info));

    // turn off backlight
    // ESP_LOGI(TAG, "backlight off");
    i2c_lcd1602_set_backlight(lcd_info, false);

    // turn on backlight
    // ESP_LOGI(TAG, "backlight on");
    i2c_lcd1602_set_backlight(lcd_info, true);
}


enum SetMode {
  Run,
  SetSetbackTemp,
  SetOverrideTemp,
  SetOverrideTime,
  SetAnticipation,
  SetCyclesPerHour,
  SetTempOffset
} set_mode;

DeviceAddress tempSensors[1];

void getTempAddresses(DeviceAddress *tempSensorAddresses) {
	unsigned int numberFound = 0;
	reset_search();
	// search for 2 addresses on the oneWire protocol
	while (search(tempSensorAddresses[numberFound],true)) {
		numberFound++;
		if (numberFound == 1) break;
	}
	// if 2 addresses aren't found 
	while (numberFound != 1) {
		numberFound = 0;
		vTaskDelay(200 / portTICK_PERIOD_MS);
		// search in the loop for the temp sensors as they may hook them up
		reset_search();
		while (search(tempSensorAddresses[numberFound],true)) {
			numberFound++;
			if (numberFound == 1) break;
		}
	}
	return;
}
void setup()
{
  // set up inputs and output
  gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(HEATING_LED_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(OVERRIDE_LED_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(RELAY_PIN, LOW);
  gpio_set_level(HEATING_LED_PIN, HIGH);
  gpio_set_level(OVERRIDE_LED_PIN, HIGH);

  // activate LCD module
  // Set up I2C
    i2c_master_init();
    i2c_port_t i2c_num = I2C_MASTER_NUM;
    uint8_t address = LCD1602_I2C_ADDRESS;

    // Set up the SMBus
    smbus_info_t * smbus_info = smbus_malloc();
    ESP_ERROR_CHECK(smbus_init(smbus_info, i2c_num, address));
    ESP_ERROR_CHECK(smbus_set_timeout(smbus_info, 1000 / portTICK_RATE_MS));

    // Set up the LCD1602 device with backlight off
    lcd_info = i2c_lcd1602_malloc();
    ESP_ERROR_CHECK(i2c_lcd1602_init(lcd_info, smbus_info, true,
                                     LCD_NUM_ROWS, LCD_NUM_COLUMNS, LCD_NUM_VISIBLE_COLUMNS));

    ESP_ERROR_CHECK(i2c_lcd1602_reset(lcd_info));

    // turn off backlight
    
  i2c_lcd1602_home(lcd_info);
  i2c_lcd1602_write_string(lcd_info, "Hello!");

 

  // initialize temp sensor
  ds18b20_init(TEMP_BUS);
  getTempAddresses(tempSensors);
  ds18b20_setResolution(tempSensors,1,12);

  // initialize variables
  temp_mode = Setback;
  set_mode = Run;
  button_events = button_init(PIN_BIT(SW_START_PIN) | PIN_BIT(SW_SET_PIN) | PIN_BIT(SW_CANCEL_PIN)  | PIN_BIT(SW_UP_PIN) |PIN_BIT(SW_DOWN_PIN));

  //Open NVS storage for temperature settings

  printf("NVS Demo, esp32-tutorial\n\n");
	
	// initialize NVS flash
	esp_err_t err = nvs_flash_init();
	
	// if it is invalid, try to erase it
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
		
		printf("Got NO_FREE_PAGES error, trying to erase the partition...\n");
		
		// find the NVS partition
        const esp_partition_t* nvs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);      
		if(!nvs_partition) {
			
			printf("FATAL ERROR: No NVS partition found\n");
			while(1) vTaskDelay(10 / portTICK_PERIOD_MS);
		}
        		// erase the partition
        err = (esp_partition_erase_range(nvs_partition, 0, nvs_partition->size));
		if(err != ESP_OK) {
			printf("FATAL ERROR: Unable to erase the partition\n");
			while(1) vTaskDelay(10 / portTICK_PERIOD_MS);
		}
		printf("Partition erased!\n");
		
		// now try to initialize it again
		err = nvs_flash_init();
		if(err != ESP_OK) {
			
			printf("FATAL ERROR: Unable to initialize NVS\n");
			while(1) vTaskDelay(10 / portTICK_PERIOD_MS);
		}
	}
	printf("NVS init OK!\n");
	
	// open the partition in RW mode
	err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
		
		printf("FATAL ERROR: Unable to open NVS\n");
		while(1) vTaskDelay(10 / portTICK_PERIOD_MS);
	}
	printf("NVS open OK\n");

	int32_t value = 0;
  float fvalue = 0.0;
  char* buff[128];
  
    

    err = nvs_get_i32(my_handle, str_setback, &value);
    if (err != ESP_OK){
      value = 0;
    }
	  setback_temp = value;
    value = 0;

    err = nvs_get_i32(my_handle, str_override, &value);
    if (err != ESP_OK){
      value = 0;
    }
	  override_temp = value;
    value = 0;
    
    err = nvs_get_i32(my_handle, str_min_override, &value);
    if (err != ESP_OK){
      value = 0;
    }
	  override_minutes = value;
    value = 0;

    err = nvs_get_i32(my_handle, str_cycle_limit, &value);
    if (err != ESP_OK){
      value = 0;
    }
	  cycle_limit = value;
    value = 0;

    err = nvs_get_i32(my_handle, str_cycles_per_hour, &value);
    if (err != ESP_OK){
      value = 0;
    }
	  cycles_per_hour = value;
    value = 0;

    err = nvs_get_i32(my_handle, str_anticipation, &value);
    if (err != ESP_OK){
      value = 0;
    }
	  anticipation = value;


    size_t required_size;
    err = nvs_get_str(my_handle, str_temp_offset, NULL, &required_size);
    if (err != ESP_OK) {
      printf("\nError Temperature Override to get string size! (%04X)\n", err);
      temp_offset = 0.0;
    } else {
      char* temp_offset_len = malloc(required_size);
      nvs_get_str(my_handle, str_temp_offset, temp_offset_len, &required_size);
    
      temp_offset = atof(temp_offset_len);
    }

  
  bool upflag = false;
  if (setback_temp < MIN_SETBACK_TEMP) {
    setback_temp = MIN_SETBACK_TEMP;
    upflag = true;
  }
  if (setback_temp > MAX_SETBACK_TEMP) {
    setback_temp = MAX_SETBACK_TEMP;
    upflag = true;
  }
  
  if (override_temp < MIN_OVERRIDE_TEMP) {
    override_temp = MIN_OVERRIDE_TEMP;
    upflag = true;
  }
  if (override_temp > MAX_OVERRIDE_TEMP) {
    override_temp = MAX_OVERRIDE_TEMP;
    upflag = true;
  }
  
  if (override_minutes < MIN_OVERRIDE_TIME) {
    override_minutes = MIN_OVERRIDE_TIME;
    upflag = true;
  }
  if (override_minutes > MAX_OVERRIDE_TIME) {
    override_minutes = MAX_OVERRIDE_TIME;
    upflag = true;
  }

  if (cycles_per_hour < MIN_CYCLES_PER_HOUR) {
    cycles_per_hour = MIN_CYCLES_PER_HOUR;
    cycle_limit = (cycles_per_hour != MIN_CYCLES_PER_HOUR);
    upflag = true;
  }
  if (cycles_per_hour > MAX_CYCLES_PER_HOUR) {
    cycles_per_hour = MAX_CYCLES_PER_HOUR;
    cycle_limit = (cycles_per_hour != MIN_CYCLES_PER_HOUR);
    upflag = true;
  }
  
  if (anticipation < MIN_ANTICIPATION) {
    anticipation = MIN_ANTICIPATION;
    upflag = true;
  } else if (anticipation > MAX_ANTICIPATION) {
    anticipation = MAX_ANTICIPATION;
    upflag = true;
  } else {
    anticipation = MAX_ANTICIPATION;
    upflag = true;
  }
  if (upflag) {
    err = nvs_commit(my_handle);
		if(err != ESP_OK) {
			printf("\nError in commit! (%04X)\n", err);
		}
  }
}

void UpdateEEPROM() {
  int old_setback_temp;
  int old_override_temp;
  int old_override_minutes;
  bool old_cycle_limit;
  int old_cycles_per_hour;
  float old_anticipation;
  float old_temp_offset;
  int value;
  char* buff[128];



  value = 0;
  nvs_get_i32(my_handle, str_setback, &value);
  old_setback_temp = value;
  nvs_get_i32(my_handle, str_override, &value);
  old_override_temp = value;
  nvs_get_i32(my_handle, str_min_override, &value);
  old_override_minutes = value;
  nvs_get_i32(my_handle, str_cycle_limit, &value);
  old_cycle_limit = value;
  nvs_get_i32(my_handle, str_cycles_per_hour, &value);
  old_cycles_per_hour = value;
  nvs_get_i32(my_handle, str_anticipation, &value);
  old_anticipation = value;

  size_t required_size;
  nvs_get_str(my_handle, str_temp_offset, NULL, &required_size);
  char* temp_offset_len = malloc(required_size);
  nvs_get_str(my_handle, str_temp_offset, temp_offset_len, &required_size);
    
  old_temp_offset = atof(temp_offset_len);
  
 


  
  if (setback_temp != old_setback_temp)         nvs_set_i32(my_handle,str_setback , setback_temp);
  if (override_temp != old_override_temp)       nvs_set_i32(my_handle,str_override, override_temp);
  if (override_minutes != old_override_minutes) nvs_set_i32(my_handle,str_min_override, override_minutes);
  if (cycle_limit != old_cycle_limit)           nvs_set_i32(my_handle, str_cycle_limit, cycle_limit);
  if (cycles_per_hour != old_cycles_per_hour)   nvs_set_i32(my_handle, str_cycles_per_hour, cycles_per_hour);
  if (anticipation != old_anticipation)         nvs_set_i32(my_handle, str_anticipation, anticipation);
  if (temp_offset != old_temp_offset) {
    ftoa(temp_offset, buff,1);
    nvs_set_str(my_handle, str_temp_offset, buff);
    }
  

    nvs_commit(my_handle);

}


void ModeControl() {

  bool set_pressed = false;
  bool up_pressed = false;
  bool down_pressed = false;

  if (xQueueReceive(button_events, &ev, 1000/portTICK_PERIOD_MS)) {
    //Handle Override and Cancel 
		if ((ev.pin == SW_START_PIN) && (ev.event == BUTTON_DOWN)) {
			ESP_LOGI("Button Start", "Down");
      temp_mode = Override;
      override_start_time = millis();
    }
		if ((ev.pin == SW_CANCEL_PIN) && (ev.event == BUTTON_DOWN)) {
			ESP_LOGI("Button Cancel", "Down");
      temp_mode = Setback;
      time_left = 0;
		}
  // Handle Set/Up/Down
  //
    if((ev.pin == SW_SET_PIN) && (ev.event == BUTTON_DOWN)) {
      set_pressed = true;
    }
    if((ev.pin == SW_UP_PIN) && (ev.event == BUTTON_DOWN)) {
      up_pressed = true;
    }
    if((ev.pin == SW_DOWN_PIN) && (ev.event == BUTTON_DOWN)) {
      down_pressed = true;
    }
  }

  if (set_pressed || up_pressed || down_pressed) {
    timeout_time = millis() + SET_MODE_TIMEOUT;
  }

  switch (set_mode) {
    case Run:
      if (set_pressed) {
        set_mode = SetSetbackTemp;
      }
      break;

    case SetSetbackTemp:
      if (set_pressed) set_mode = SetOverrideTemp;
      if (up_pressed) {
        if (setback_temp < MAX_SETBACK_TEMP) {
          setback_temp += 1;
        }
      }
      if (down_pressed) {
        if (setback_temp > MIN_SETBACK_TEMP) {
          setback_temp -= 1;
        }
      }
      break;

    case SetOverrideTemp:
      if (set_pressed) set_mode = SetOverrideTime;
      if (up_pressed) {
        if (override_temp < MAX_OVERRIDE_TEMP) {
          override_temp += 1;
        }
      }
      if (down_pressed) {
        if (override_temp > MIN_OVERRIDE_TEMP) {
          override_temp -= 1;
        }
      }
      break;

    case SetOverrideTime:
      if (set_pressed) set_mode = SetAnticipation;
      if (up_pressed) {
        if (override_minutes < MAX_OVERRIDE_TIME) {
          override_minutes += 5;
        } else {
          override_minutes = MIN_OVERRIDE_TIME;
        }
      }
      if (down_pressed) {
        if (override_minutes > MIN_OVERRIDE_TIME) {
          override_minutes -= 5;
          if (override_minutes < 0) override_minutes = MIN_OVERRIDE_TIME;
        } else {
          override_minutes = MAX_OVERRIDE_TIME;
        }
      }
      break;

    case SetAnticipation:
      if (set_pressed) set_mode = SetCyclesPerHour;
      if (up_pressed) {
        if (anticipation < MAX_ANTICIPATION) {
          anticipation += 0.5;
        }
      }
      if (down_pressed) {
        if (anticipation > MIN_ANTICIPATION) {
          anticipation -= 0.5;
          if (anticipation < 0) anticipation = 0;
        }
      }
      break;

    case SetCyclesPerHour:
      if (set_pressed) {
        set_mode = SetTempOffset;
      }
      if (up_pressed) {
        if (cycles_per_hour < MAX_CYCLES_PER_HOUR) {
          cycles_per_hour += 1;
        } else {
          cycles_per_hour = MIN_CYCLES_PER_HOUR; // wraps
        }
      }
      if (down_pressed) {
        if (cycles_per_hour > MIN_CYCLES_PER_HOUR) {
          cycles_per_hour -= 1;
        } else {
          cycles_per_hour = MAX_CYCLES_PER_HOUR; // wraps
        }
      }
      cycle_limit = (cycles_per_hour != MIN_CYCLES_PER_HOUR);
      break;

      case SetTempOffset:
      if (set_pressed) {
        set_mode = Run;
        UpdateEEPROM();
      }
      if (up_pressed) {
        if (temp_offset < MAX_TEMP_OFFSET) {
          temp_offset += 0.5;
        } 
      }
      if (down_pressed) {
        if (temp_offset > MIN_TEMP_OFFSET) {
          temp_offset -= 0.5;
        } 
        if (temp_offset< MIN_TEMP_OFFSET) temp_offset = MIN_TEMP_OFFSET;
      }
      
      break;

    default:
      break;
  }

  // Leave set mode if no buttons are pressed for a period of time
  //
  if ((set_mode != Run) && (((long) (millis() - timeout_time) >= 0))) {
    set_mode = Run;
    UpdateEEPROM();
  }

}


// This is the heart of the thermostat: the control loop
// It reads the temperature and engages or disengages the relay as needed
void TempControl() {
  // read the temperature every 100th frame if we are not in set mode - o.w. there can be delays between
  // pressing a button and it taking effect
  //

   if ((set_mode == Run) && ((frame % 1) == 0)) {
    float temp_curr_temp;

   
    ds18b20_requestTemperatures();
		temp_curr_temp = ds18b20_getTempF((DeviceAddress *)tempSensors[0]);
    printf("reading temp %f\n",temp_curr_temp);

    if ( fabs(temp_curr_temp - DEVICE_DISCONNECTED_F) > 1.0e-5 ){ // ignore bad reading
      
		  curr_temp = temp_curr_temp;
      curr_temp -=temp_offset;

    }

    
   }

  // see if the override has timed out
  //
  override_finish_time = override_start_time + (60000UL * (unsigned long) override_minutes);
  if ((temp_mode == Override) && (((long) (millis() - override_finish_time) >= 0))) {
    temp_mode = Setback;
  }

  // calculate set point and whether it's OK to start a new cycle based on cycles_per_hour
  //
  curr_set_point = (temp_mode == Override) ? override_temp : setback_temp;

  unsigned long next_cycle_time = cycle_limit ? (last_cycle_time + (3600000UL / (unsigned long) cycles_per_hour)) : last_cycle_time;

  if (!heating
      && (curr_temp < (((float) curr_set_point) - anticipation))
      && (!cycle_limit || (((long)( millis() - next_cycle_time ) >= 0)))) {
    heating = true;
    last_cycle_time = millis();
  }

  if (heating && (curr_temp >= (float) curr_set_point)) {
    heating = false;
  }

  if (heating) {
    digitalWrite(RELAY_PIN, HIGH);
  } else {
    digitalWrite(RELAY_PIN, LOW);
  }
}
void print_minutes(int t) {
  int hours = t / 60;
  int minutes = t % 60;
  char buffer [sizeof(int)*8+1];
  if (hours > 0) {
    i2c_lcd1602_write_string(lcd_info,itoa(hours,buffer,10));
  } else {
    i2c_lcd1602_write_string(lcd_info," ");
  }
  i2c_lcd1602_write_string(lcd_info,":");
  if (minutes < 10) {
    i2c_lcd1602_write_string(lcd_info,"0");
  }
  i2c_lcd1602_write_string(lcd_info,itoa(minutes,buffer,10));
  i2c_lcd1602_write_string(lcd_info," ");
}


void Display() {

  //scratch buffer for display
  char buffer [sizeof(int)*8+1];
  
  // set status LEDs
  digitalWrite(HEATING_LED_PIN, heating);
  if (temp_mode == Override) {
    digitalWrite(OVERRIDE_LED_PIN, HIGH);
  }
  else{
    digitalWrite(OVERRIDE_LED_PIN, LOW);
  }  

  
  // write to LCD matrix display
  // set cursor to 0,0
  i2c_lcd1602_home(lcd_info);

  switch (set_mode) {

    // RUN
    case Run:
      i2c_lcd1602_write_string(lcd_info,"Temp:      ");
      i2c_lcd1602_write_string(lcd_info,itoa(curr_temp, buffer, 10));
      i2c_lcd1602_write_string(lcd_info,"F  ");
      

      // go to start of 2nd line
      i2c_lcd1602_move_cursor(lcd_info, 0, 1);

      if ((temp_mode == Setback) || (frame < FRAME_HALF_LIMIT)) {
        i2c_lcd1602_write_string(lcd_info,"Set Point: ");
        i2c_lcd1602_write_string(lcd_info, itoa(curr_set_point, buffer, 10));
        i2c_lcd1602_write_string(lcd_info,"F  ");
        
      }
      if ((temp_mode == Override) && (frame >= FRAME_HALF_LIMIT)) {
        i2c_lcd1602_write_string(lcd_info,"Ovrd Time: ");
        time_left = (override_finish_time - millis());
        print_minutes ((int) (time_left / 60000L) + 1);
      }
      break;

    // Setback Temp
    case SetSetbackTemp:
      i2c_lcd1602_write_string(lcd_info,"Setback Temp:   ");
      i2c_lcd1602_move_cursor(lcd_info, 0, 1);
      i2c_lcd1602_write_string(lcd_info,itoa(setback_temp,buffer,10));
      i2c_lcd1602_write_string(lcd_info,"              ");
      break;

    // Override Temp
    case SetOverrideTemp:
      i2c_lcd1602_write_string(lcd_info,"Override Temp:  ");
      i2c_lcd1602_move_cursor(lcd_info, 0, 1);
      i2c_lcd1602_write_string(lcd_info,itoa(override_temp,buffer,10));
      i2c_lcd1602_write_string(lcd_info,"              ");
      break;

    // Override Time
    case SetOverrideTime:
      i2c_lcd1602_write_string(lcd_info,"Override Time:  ");
      i2c_lcd1602_move_cursor(lcd_info, 0, 1);
      print_minutes(override_minutes);
      i2c_lcd1602_write_string(lcd_info,"            ");
      break;

    // Anticipation
    case SetAnticipation:
      i2c_lcd1602_write_string(lcd_info,"Anticipation:   ");
      i2c_lcd1602_move_cursor(lcd_info, 0, 1);
      i2c_lcd1602_write_string(lcd_info,itoa(anticipation,buffer,10));
      i2c_lcd1602_write_string(lcd_info,"             ");
      break;

    // Cycles/Hour
    case SetCyclesPerHour:
      i2c_lcd1602_write_string(lcd_info,"Cycles/Hour:    ");
      i2c_lcd1602_move_cursor(lcd_info, 0, 1);
      if (cycle_limit) {
        i2c_lcd1602_write_string(lcd_info,itoa(cycles_per_hour,buffer,10));
        i2c_lcd1602_write_string(lcd_info,"              ");
      } else {
        i2c_lcd1602_write_string(lcd_info,"-               ");
      }
      break;
    
    case SetTempOffset:
      i2c_lcd1602_write_string(lcd_info,"Temp Offset:   ");
      i2c_lcd1602_move_cursor(lcd_info, 0, 1);
      printf("float %f", temp_offset);
      ftoa(temp_offset, buffer, 1);
      i2c_lcd1602_write_string(lcd_info,buffer);
      i2c_lcd1602_write_string(lcd_info,"             ");
      break;

    default:
      break;

  }

  
  frame += 1;
  frame = frame % FRAME_LIMIT;
  
}


void app_main()
{
  setup();
#ifdef WIFI_ENABLED
// tasks related to wifi-based configuration
    TaskHandle_t xBlinkHandle = NULL;
    wificonfig();
    if (xBlinkHandle != NULL) {
        vTaskDelete (xBlinkHandle);
    }
    load_aps();
    if (ap_count == 0) {
        // if no valid ssids -> go back to config mode
        ESP_LOGI (TAG, "Whoa! No ssids configured");
        trigger_wificonfig();
    }
    xTaskCreate(&check_set, "check_set", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK( esp_event_loop_create_default() );


    initialize_wifi();
    initialize_mqtt();

   if ((mqtt_client != NULL) && (wificonfig_vals_mqtt.update != 0)) {
        xTaskCreate(&update_loop, "update_loop", 4096, NULL, 5, NULL);
    }
  #endif
  
  while(1) {
    ModeControl ();
    TempControl ();
    Display ();
  }
  //delay(LOOP_DELAY);
  vTaskDelay(LOOP_DELAY / portTICK_PERIOD_MS);
}