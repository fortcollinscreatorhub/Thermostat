// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <string.h>
#include <sys/socket.h>
#include <vector>

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_tls_crypto.h>
#include <nvs.h>

#include "fcch_connmgr/cm.h"
#include "fcch_connmgr/cm_conf.h"
#include "fcch_connmgr/cm_net.h"
#include "fcch_connmgr/cm_util.h"
#include "cm_admin.h"
#include "cm_app_version.h"
#include "cm_http.h"
#include "cm_nvs.h"

struct cm_http_action {
    const char *name;
    cm_http_action_description_func *description;
    cm_http_action_func *function;
    httpd_uri_t uri;
};

static const char *TAG = "cm_http";
static const size_t CM_HTTP_MAX_USER_FORM_POST_DATA = 512;
static const size_t CM_HTTP_MAX_FILE_UPLOAD_POST_DATA = 4096;

extern const char cm_http_styles_start[] asm("_binary_styles_html_start");
extern const char cm_http_styles_end[]   asm("_binary_styles_html_end");

static const char *cm_http_auth_header;
static const char *cm_http_www_authenticate;
static httpd_handle_t cm_http_server = NULL;
static std::vector<cm_http_action *> cm_http_actions;

static inline void cm_http_chunks_done(httpd_req_t *req) {
    httpd_resp_send_chunk(req, NULL, 0);
}

static char *html_encode(const char *buf) {
    // strlen(&#xNN;) == 6
    char *out = (char *)malloc((strlen(buf) * 6) + 1);
    uint8_t *d = (uint8_t *)out;
    uint8_t *s = (uint8_t *)buf;
    while (*s) {
        uint8_t c = *s++;
        bool legal =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            (c == '.') ||
            (c == '-') ||
            (c == '_');
        if (legal) {
            *d++ = c;
        } else {
            *d++ = '&';
            *d++ = '#';
            *d++ = 'x';
            *d++ = cm_util_uint_to_hex_char(c >> 4);
            *d++ = cm_util_uint_to_hex_char(c & 0xf);
            *d++ = ';';
        }
    }
    *d = '\0';
    return out;
}

static void cm_http_send_nvs_str(httpd_req_t *req, cm_conf_item *item) {
    AutoFree<const char> val;
    esp_err_t err = cm_conf_read_as_str(item, &val.val);
    if (err != ESP_OK) {
        const char* placeholder;
        switch (item->type) {
        case CM_CONF_ITEM_TYPE_U32:
        case CM_CONF_ITEM_TYPE_U16:
            placeholder = "";
            break;
        default:
            placeholder = "0";
        }
        httpd_resp_sendstr_chunk(req, placeholder);
        return;
    }
    if ((item->type == CM_CONF_ITEM_TYPE_PASS) && (val.val[0] != '\0')) {
        httpd_resp_sendstr_chunk(req, "****");
    } else if (val.val[0] != '\0') {
        AutoFree<char> s(html_encode(val.val));
        httpd_resp_sendstr_chunk(req, s.val);
    }
}

static void uri_decode(char *buf) {
    uint8_t *d = (uint8_t *)buf;
    uint8_t *s = (uint8_t *)buf;
    while (*s) {
        if (s[0] == '%') {
            if (!s[1] || !s[2])
                break;
            *d++ =
                (cm_util_hex_char_to_uint(s[1]) << 4) |
                (cm_util_hex_char_to_uint(s[2]) << 0);
                s += 3;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0';
}

static esp_err_t cm_http_recv_nvs_str(const char *post_body, cm_conf_item *item) {
    char buf[33];
    esp_err_t err;
    err = httpd_query_key_value(post_body, item->slug_name, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot find item in POST data: %d\n", err);
        return err;
    }
    uri_decode(buf);
    if (
        (item->type == CM_CONF_ITEM_TYPE_PASS) &&
        // 4 chars is too short for a password, so the user should never
        // enter **** as a password, so we can use it as the "sentinel" to
        // indicate an unmodified mask string passed back from the output of
        // cm_http_send_nvs_str().
        (!strcmp(buf, "****"))
    ) {
        return ESP_OK;
    }
    err = cm_conf_write_as_str(item, buf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot write nvs: %d\n", err);
        return err;
    }
    return ESP_OK;
}

static void cm_http_send_page_top(
    httpd_req_t *req,
    const char* page_name1,
    const char* page_name2
) {
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1,user-scalable=no\">");
    httpd_resp_send_chunk(req, cm_http_styles_start,
        cm_http_styles_end - cm_http_styles_start);
    httpd_resp_sendstr_chunk(req,
        "<title>");
    httpd_resp_sendstr_chunk(req, cm_net_hostname);
    if ((page_name1 != NULL) && (page_name1[0] != '\0')) {
        httpd_resp_sendstr_chunk(req, "/");
        httpd_resp_sendstr_chunk(req, page_name1);
    }
    if ((page_name2 != NULL) && (page_name2[0] != '\0')) {
        httpd_resp_sendstr_chunk(req, "/");
        httpd_resp_sendstr_chunk(req, page_name2);
    }
    httpd_resp_sendstr_chunk(req,
        "</title>"
        "</head>"
        "<body>"
        "<p class=\"hn\">http://");
    httpd_resp_sendstr_chunk(req, cm_net_hostname);
    httpd_resp_sendstr_chunk(req, ".local");
    httpd_resp_sendstr_chunk(req, "</p><p class=\"pn\">");
    if ((page_name1 != NULL) && (page_name1[0] != '\0')) {
        httpd_resp_sendstr_chunk(req, page_name1);
    }
    if ((page_name2 != NULL) && (page_name2[0] != '\0')) {
        httpd_resp_sendstr_chunk(req, "/");
        httpd_resp_sendstr_chunk(req, page_name2);
    }
    httpd_resp_sendstr_chunk(req,
        "</p>"
        "<div class=\"bs\">");
}

static void cm_http_send_nav_button(
    httpd_req_t *req,
    const char *path1,
    const char *path2,
    const char *name
) {
    httpd_resp_sendstr_chunk(req, "<a class=\"b h\" href=\"");
    if ((path1 != NULL) && (path1[0] != '\0'))
        httpd_resp_sendstr_chunk(req, path1);
    if ((path2 != NULL) && (path2[0] != '\0'))
        httpd_resp_sendstr_chunk(req, path2);
    httpd_resp_sendstr_chunk(req, "\">");
    httpd_resp_sendstr_chunk(req, name);
    httpd_resp_sendstr_chunk(req, "</a>");
}

static void cm_http_send_action_form(
    httpd_req_t *req,
    const char *path,
    const char *name
) {
    httpd_resp_sendstr_chunk(req, "<form action=\"");
    httpd_resp_sendstr_chunk(req, path);
    httpd_resp_sendstr_chunk(req, "\" method=\"post\"><button class=\"b h\">");
    httpd_resp_sendstr_chunk(req, name);
    httpd_resp_sendstr_chunk(req, "</button></form>");
}

static void cm_http_send_page_bottom(httpd_req_t *req) {
    httpd_resp_sendstr_chunk(req,
        "</div>"
        "<div class=\"ver\">");
    httpd_resp_sendstr_chunk(req,
        cm_app_version);
    httpd_resp_sendstr_chunk(req,
        "</div>"
        "</body>"
        "</html>");
}

enum cm_http_tristate {
    CM_HTTP_TRISTATE_UNKNOWN,
    CM_HTTP_TRISTATE_YES,
    CM_HTTP_TRISTATE_NO,
};
typedef enum cm_http_tristate cm_http_tristate_t;

static cm_http_tristate_t cm_http_is_ap_client(
    httpd_req_t *req,
    esp_netif_ip_info_t *ap_ip_info
) {
    esp_err_t err = esp_netif_get_ip_info(cm_net_if_ap, ap_ip_info);
    if (err != ESP_OK)
        return CM_HTTP_TRISTATE_UNKNOWN;

    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0)
        return CM_HTTP_TRISTATE_UNKNOWN;

    union {
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } addr;
    socklen_t addr_size = sizeof(addr);
    if (getsockname(sockfd, (struct sockaddr *)&addr, &addr_size) != 0)
        return CM_HTTP_TRISTATE_UNKNOWN;
    uint32_t local_ip = 0;
    if (addr.in.sin_family == AF_INET) {
        local_ip = addr.in.sin_addr.s_addr;
    } else if (addr.in6.sin6_family == AF_INET6) {
        local_ip = addr.in6.sin6_addr.un.u32_addr[3];
    }
    // Note: These are both in network order:
    if (local_ip == ap_ip_info->ip.addr)
        return CM_HTTP_TRISTATE_YES;

    return CM_HTTP_TRISTATE_NO;
}

static bool cm_http_check_is_auth(httpd_req_t *req) {
    if (!cm_http_auth_header) {
        ESP_LOGD(TAG, "!cm_http_auth_header");
        return true;
    }

    esp_netif_ip_info_t ap_ip_info;
    if (cm_http_is_ap_client(req, &ap_ip_info) == CM_HTTP_TRISTATE_YES) {
        ESP_LOGD(TAG, "cm_http_is_ap_client");
        return true;
    }

    size_t auth_len = httpd_req_get_hdr_value_len(req, "Authorization");
    ESP_LOGD(TAG, "auth_len %d", auth_len);
    if (auth_len < 1) {
        return false;
    }

    size_t buf_len = auth_len + 1;
    AutoFree<char>auth_hdr((char *)malloc(buf_len));
    assert(auth_hdr.val != NULL);

    ESP_ERROR_CHECK(httpd_req_get_hdr_value_str(req, "Authorization",
        auth_hdr.val, buf_len));
    auth_hdr.val[auth_len] = '\0';

    if (strcmp(auth_hdr.val, cm_http_auth_header)) {
        ESP_LOGD(TAG, "auth_hdr '%s'", auth_hdr.val);
        ESP_LOGD(TAG, "cm_http_auth_header '%s'", cm_http_auth_header);
        return false;
    }

    ESP_LOGD(TAG, "Authenticated");
    return true;
}

static esp_err_t cm_http_check_auth(httpd_req_t *req, bool *authorized) {
    *authorized = cm_http_check_is_auth(req);
    if (!*authorized) {
        ESP_LOGD(TAG, "UNauthenticated");
        httpd_resp_set_status(req, "401 UNAUTHORIZED");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "WWW-Authenticate", cm_http_www_authenticate);
        httpd_resp_send(req, NULL, 0);
    }

    ESP_LOGD(TAG, "Authenticated");
    return ESP_OK;
}

#define CM_HTTP_CHECK_AUTH do { \
    bool authorized; \
    esp_err_t err = cm_http_check_auth(req, &authorized); \
    if (err != ESP_OK) \
        return err; \
    if (!authorized) \
        /* cm_http_check_auth sent the auth prompt */ \
        return ESP_OK; \
} while (0)

static esp_err_t cm_http_home_get_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;
    cm_http_send_page_top(req, "Home", NULL);
    cm_http_send_nav_button(req, "/conf", "", "Configuration");
    cm_http_send_action_form(req, "/reboot", "Reboot");
    for (auto *action : cm_http_actions)
        cm_http_send_action_form(req, action->uri.uri, action->description());
    cm_http_send_page_bottom(req);
    cm_http_chunks_done(req);
    return ESP_OK;
}

static const httpd_uri_t cm_http_home_get_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = cm_http_home_get_handler,
    .user_ctx  = NULL
};

static esp_err_t cm_http_conf_get_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;
    cm_http_send_page_top(req, "Configuration", NULL);

    for (
        cm_conf_page *page = cm_conf_pages;
        page != NULL;
        page = page->next
    ) {
        cm_http_send_nav_button(req, "/conf/",
            page->slug_name, page->text_name);
    }
    httpd_resp_sendstr_chunk(req, "<br/>");
    cm_http_send_nav_button(req, "/export", "", "Export config");
    cm_http_send_nav_button(req, "/import", "", "Import config");
    cm_http_send_action_form(req, "/wipe", "Wipe config");
    httpd_resp_sendstr_chunk(req, "<br/>");
    cm_http_send_nav_button(req, "/", "", "Home");
    cm_http_send_page_bottom(req);
    cm_http_chunks_done(req);
    return ESP_OK;
}

static const httpd_uri_t cm_http_conf_get_uri = {
    .uri       = "/conf",
    .method    = HTTP_GET,
    .handler   = cm_http_conf_get_handler,
    .user_ctx  = NULL
};

static esp_err_t conf_page_get_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;
    cm_conf_page *page = (cm_conf_page *)req->user_ctx;

    cm_http_send_page_top(req, "Configuration", page->text_name);
    httpd_resp_sendstr_chunk(req, "<form action=\"/conf/");
    httpd_resp_sendstr_chunk(req, page->slug_name);
    httpd_resp_sendstr_chunk(req,"\" method=\"POST\">");

    for (size_t i = 0; i < page->items_count; i++) {
        cm_conf_item *item = page->items[i];
        httpd_resp_sendstr_chunk(req, "<label for=\"");
        httpd_resp_sendstr_chunk(req, item->slug_name);
        httpd_resp_sendstr_chunk(req, "\">");
        httpd_resp_sendstr_chunk(req, item->text_name);
        httpd_resp_sendstr_chunk(req, ":</label><input name=\"");
        httpd_resp_sendstr_chunk(req, item->slug_name);
        httpd_resp_sendstr_chunk(req, "\" type=\"");
        if (item->type == CM_CONF_ITEM_TYPE_PASS) {
            httpd_resp_sendstr_chunk(req, "password");
        } else {
            httpd_resp_sendstr_chunk(req, "text");
        }
        httpd_resp_sendstr_chunk(req, "\" value=\"");
        cm_http_send_nvs_str(req, item);
        httpd_resp_sendstr_chunk(req, "\"/>");
    }

    httpd_resp_sendstr_chunk(req,
        "<button class=\"b h\">Save</button>"
        "</form>");
    cm_http_send_nav_button(req, "/conf", "", "Configuration");
    cm_http_send_nav_button(req, "/", "", "Home");
    cm_http_send_page_bottom(req);
    cm_http_chunks_done(req);
    return ESP_OK;
}

static esp_err_t conf_page_post_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;
    cm_conf_page *page = (cm_conf_page *)req->user_ctx;

    if (req->content_len >= CM_HTTP_MAX_USER_FORM_POST_DATA) {
        ESP_LOGW(TAG, "POST data too large");
        return ESP_FAIL;
    }

    // FIXME: verify Content-Type==application/x-www-form-urlencoded

    AutoFree<char> post_body((char *)malloc(CM_HTTP_MAX_USER_FORM_POST_DATA));
    assert(post_body.val != NULL);
    int len_or_err = httpd_req_recv(req, post_body.val, req->content_len);
    if (len_or_err <= 0) {
        if (len_or_err == HTTPD_SOCK_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "POST data rx timeout");
            httpd_resp_send_408(req);
        } else {
            ESP_LOGW(TAG, "POST data rx other error %d", len_or_err);
        }
        return ESP_FAIL;
    }
    post_body.val[len_or_err] = '\0';

    for (size_t i = 0; i < page->items_count; i++) {
        cm_conf_item *item = page->items[i];
        ESP_LOGI(TAG, "Process item %s", item->slug_name);
        esp_err_t err = cm_http_recv_nvs_str(post_body.val, item);
        if (err != ESP_OK)
            return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Redirecting");
    const char status[] = "302 Found";
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Location", "/conf");
    httpd_resp_send(req, status, 0);
    return ESP_OK;
}

static esp_err_t cm_http_reboot_post_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;
    cm_http_send_page_top(req, "Reboot", NULL);
    cm_http_send_nav_button(req, "/", "", "Home");
    cm_http_send_page_bottom(req);
    cm_http_chunks_done(req);

    vTaskDelay(250 / portTICK_PERIOD_MS);
    cm_net_notify_upcoming_restart();
    esp_restart();

    return ESP_OK;
}

static const httpd_uri_t cm_http_reboot_post_uri = {
    .uri       = "/reboot",
    .method    = HTTP_POST,
    .handler   = cm_http_reboot_post_handler,
    .user_ctx  = NULL
};

static esp_err_t cm_http_export_get_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;

    std::stringstream configss;
    esp_err_t err = cm_nvs_export(configss);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Config export failed: %d", err);
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Content-Type", "text/plain");

    std::string cd("attachment; filename=");
    cd += cm_net_hostname;
    cd += ".cfg";
    httpd_resp_set_hdr(req, "Content-Disposition", cd.c_str());
    std::string config(configss.str());

    httpd_resp_send(req, config.c_str(), config.length());

    return ESP_OK;
}

static const httpd_uri_t cm_http_export_get_uri = {
    .uri       = "/export",
    .method    = HTTP_GET,
    .handler   = cm_http_export_get_handler,
    .user_ctx  = NULL
};

static esp_err_t cm_http_import_get_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;

    cm_http_send_page_top(req, "Import", NULL);
    httpd_resp_sendstr_chunk(req,
        "<form action=\"/import\" method=\"POST\" enctype=\"multipart/form-data\">"
        "<input type=\"file\" id=\"config\" name=\"config\"/>"
        "<button class=\"b h\">Import</button>"
        "</form>"
        "<br/>"
    );
    cm_http_send_nav_button(req, "/conf", "", "Configuration");
    cm_http_send_nav_button(req, "/", "", "Home");
    cm_http_send_page_bottom(req);
    cm_http_chunks_done(req);

    return ESP_OK;
}

static const httpd_uri_t cm_http_import_get_uri = {
    .uri       = "/import",
    .method    = HTTP_GET,
    .handler   = cm_http_import_get_handler,
    .user_ctx  = NULL
};

static esp_err_t cm_http_import_post_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;

    if (req->content_len >= CM_HTTP_MAX_FILE_UPLOAD_POST_DATA) {
        ESP_LOGW(TAG, "POST data too large");
        return ESP_FAIL;
    }

    // FIXME: verify Content-Type==multipart/form-data

    AutoFree<char> post_body((char *)malloc(CM_HTTP_MAX_FILE_UPLOAD_POST_DATA));
    assert(post_body.val != NULL);
    int len_or_err = httpd_req_recv(req, post_body.val, req->content_len);
    if (len_or_err <= 0) {
        if (len_or_err == HTTPD_SOCK_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "POST data rx timeout");
            httpd_resp_send_408(req);
        } else {
            ESP_LOGW(TAG, "POST data rx other error: %d", len_or_err);
        }
        return ESP_FAIL;
    }
    post_body.val[len_or_err] = '\0';

    char *p = post_body.val;
    char *boundary = p;
    char *boundary_end = strchr(boundary, '\r');
    if (boundary_end == NULL) {
        ESP_LOGW(TAG, "Boundary EOL missing");
        return ESP_FAIL;
    }
    *boundary_end = '\0';
    p = boundary_end + 2; // \r\n

#define CONTENT_DELIM "\r\n\r\n"
    char *content_delim = strstr(p, CONTENT_DELIM);
    if (content_delim == NULL) {
        ESP_LOGW(TAG, "End of field headers missing");
        return ESP_FAIL;
    }
    char *config = content_delim + strlen(CONTENT_DELIM);

    char *config_end = strstr(config, boundary);
    if (config_end == NULL) {
        ESP_LOGW(TAG, "End boundary missing");
        return ESP_FAIL;
    }
    config_end -= 2; // \r\n before boundary
    if (config_end < config) {
        ESP_LOGW(TAG, "End boundary missing not preceded by \r\n");
        return ESP_FAIL;
    }
    *config_end = '\0';

    esp_err_t err = cm_nvs_import(config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Config import failed: %d", err);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Redirecting");
    const char status[] = "302 Found";
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Location", "/conf");
    httpd_resp_send(req, status, 0);

    return ESP_OK;
}

static const httpd_uri_t cm_http_import_post_uri = {
    .uri       = "/import",
    .method    = HTTP_POST,
    .handler   = cm_http_import_post_handler,
    .user_ctx  = NULL
};

static esp_err_t cm_http_wipe_post_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;

    cm_nvs_wipe();

    ESP_LOGI(TAG, "Redirecting");
    const char status[] = "302 Found";
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Location", "/conf");
    httpd_resp_send(req, status, 0);
    return ESP_OK;
}

static const httpd_uri_t cm_http_wipe_post_uri = {
    .uri       = "/wipe",
    .method    = HTTP_POST,
    .handler   = cm_http_wipe_post_handler,
    .user_ctx  = NULL
};

// For AP clients, redirect to an absolute URL.
// This ensures that the hostname changes, e.g. so a client that hasn't realized
// it's in a captive portal is trying to access a URL that contains a DNS name.
// For STA clients, just redirect to / so the hostname doesn't change.
// The current URL might contain an IP, a .local hostname, a DNS hostname, etc.
static void cm_http_get_redirect_url(httpd_req_t *req, char *url, size_t url_size) {
    esp_netif_ip_info_t ap_ip_info;
    if (cm_http_is_ap_client(req, &ap_ip_info) == CM_HTTP_TRISTATE_YES) {
        snprintf(url, url_size, "http://" IPSTR "/", IP2STR(&ap_ip_info.ip));
    } else {
        snprintf(url, url_size, "/");
    }
}

static esp_err_t cm_http_404_error_handler(
    httpd_req_t *req,httpd_err_code_t http_err
) {
    ESP_LOGD(TAG, "in 404 handler");

    // A redirect is required for captive portal to operate.
    char url[32];
    cm_http_get_redirect_url(req, url, sizeof(url));

    const char status[] = "302 Found";
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Location", url);
    httpd_resp_sendstr(req, status);
    return ESP_OK;
}

static void cm_http_init_auth() {
    cm_http_auth_header = NULL;
    cm_http_www_authenticate = NULL;

    if (cm_admin_password[0] == '\0') {
        return;
    }

    AutoFree<char> user_pass = NULL;
    int sz = asprintf(&user_pass.val, "%s:%s", "admin", cm_admin_password);
    if (sz < 0) {
        ESP_LOGE(TAG, "asprintf() failed: %d", sz);
        return;
    }
    if (!user_pass.val) {
        ESP_LOGE(TAG, "user_pass == NULL");
        return;
    }

    size_t sz_b64 = 0;
    // No error check; output buffer size 0 is always too small
    esp_crypto_base64_encode(NULL, 0, &sz_b64,
        (unsigned char *)user_pass.val, sz);

    // 6: The length of the "Basic " string
    // sz_b64: Number of bytes for a base64 encode format
    // 1: NUL
    AutoFree<char> auth_header((char *)calloc(1, 6 + sz_b64 + 1));
    if (!auth_header.val) {
        ESP_LOGE(TAG, "auth_header == NULL");
        return;
    }
    strcpy(auth_header.val, "Basic ");
    size_t sz_b64_2;
    int ret = esp_crypto_base64_encode((unsigned char *)auth_header.val + 6,
        sz_b64 + 1, &sz_b64_2, (unsigned char *)user_pass.val, sz);
    if (ret != 0) {
        ESP_LOGE(TAG, "base64_encode(user_pass) failed: %d", ret);
        return;
    }

    AutoFree<char> www_auth;
    sz = asprintf(&www_auth.val, "Basic realm=\"%s\"", cm_net_hostname);
    if (!www_auth.val) {
        ESP_LOGE(TAG, "cm_http_www_authenticate == NULL");
        return;
    }

    cm_http_auth_header = auth_header.val;
    auth_header.val = nullptr;
    cm_http_www_authenticate = www_auth.val;
    www_auth.val = nullptr;
}

void cm_http_init() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 24;
    config.lru_purge_enable = true;

    cm_http_init_auth();

    ESP_ERROR_CHECK(httpd_start(&cm_http_server, &config));
    ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, &cm_http_home_get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, &cm_http_conf_get_uri));

    for (
        cm_conf_page *page = cm_conf_pages;
        page != NULL;
        page = page->next
    ) {
        char *url = NULL;
        asprintf(&url, "/conf/%s", page->slug_name);
        assert(url != NULL);

        httpd_uri_t *conf_page_get_uri =
            (httpd_uri_t *)malloc(sizeof *conf_page_get_uri);
        assert(conf_page_get_uri != NULL);
        memset(conf_page_get_uri, 0, sizeof(*conf_page_get_uri));
        conf_page_get_uri->uri = url;
        conf_page_get_uri->method = HTTP_GET;
        conf_page_get_uri->handler= conf_page_get_handler;
        conf_page_get_uri->user_ctx = page;
        ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, conf_page_get_uri));

        httpd_uri_t *conf_page_post_uri =
            (httpd_uri_t *)malloc(sizeof *conf_page_post_uri);
        assert(conf_page_post_uri != NULL);
        memset(conf_page_post_uri, 0, sizeof(*conf_page_post_uri));
        conf_page_post_uri->uri = url;
        conf_page_post_uri->method = HTTP_POST;
        conf_page_post_uri->handler = conf_page_post_handler;
        conf_page_post_uri->user_ctx = page;
        ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, conf_page_post_uri));
    }

    ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, &cm_http_reboot_post_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, &cm_http_export_get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, &cm_http_import_get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, &cm_http_import_post_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, &cm_http_wipe_post_uri));
    ESP_ERROR_CHECK(httpd_register_err_handler(cm_http_server, HTTPD_404_NOT_FOUND,
        cm_http_404_error_handler));
    ESP_LOGI(TAG, "Listening on port %d", config.server_port);
}


static esp_err_t cm_http_action_post_handler(httpd_req_t *req) {
    CM_HTTP_CHECK_AUTH;
    size_t action_idx = (size_t)req->user_ctx;
    auto *action = cm_http_actions.at(action_idx);

    action->function();

    ESP_LOGI(TAG, "Redirecting");
    const char status[] = "302 Found";
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, status, 0);
    return ESP_OK;
}

void cm_http_register_home_action(
    const char *name,
    cm_http_action_description_func *description,
    cm_http_action_func *function
) {
    char *uri;
    int sz = asprintf(&uri, "/action/%s", name);
    assert(sz > 0);
    assert(uri != nullptr);

    auto *action = new cm_http_action{
        .name = name,
        .description = description,
        .function = function,
        .uri = {
            .uri = uri,
            .method = HTTP_POST,
            .handler = cm_http_action_post_handler,
            .user_ctx = NULL,
        },
    };
    action->uri.user_ctx = (void*)(cm_http_actions.size());
    cm_http_actions.push_back(action);
    ESP_ERROR_CHECK(httpd_register_uri_handler(cm_http_server, &action->uri));
}
