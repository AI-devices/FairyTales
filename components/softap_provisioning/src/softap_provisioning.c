#include "softap_provisioning.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_system.h"

#ifndef CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM
#define CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM 10
#endif

#ifndef CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM
#define CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM 32
#endif

#ifndef CONFIG_ESP_WIFI_TX_BUFFER_TYPE
#define CONFIG_ESP_WIFI_TX_BUFFER_TYPE 1
#endif

#ifndef CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF
#define CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF 0
#endif

#ifndef CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM
#define CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM 7
#endif

#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "settings_store.h"

static const char *TAG = "softap_prov";

static const char *AP_SSID_BASE = "ESP32-P4-Config";
static const char *AP_PASSWORD = "configure123";
static const char *AP_DEFAULT_URL = "http://192.168.4.1";

enum {
    DNS_PORT = 53,
    HTTP_PORT = 80,
    MAX_FORM_BODY = 1024,
    PROVISIONING_STOP_TASK_STACK = 8192,
    PROVISIONING_DNS_TASK_STACK = 4096,
};

static httpd_handle_t s_http_server = NULL;
static TaskHandle_t s_dns_task = NULL;
static int s_dns_sock = -1;
static bool s_dns_running = false;
static esp_netif_t *s_ap_netif = NULL;
static bool s_portal_running = false;
static bool s_portal_stopping = false;
static bool s_name_saved = false;
static char s_ap_ssid[33] = "ESP32-P4-Config";
static char s_ap_url[32] = "http://192.168.4.1";

static const char *kConfigPageTemplate =
    "<!doctype html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Fairy Tale Name Setup</title>"
    "<style>"
    ":root{color-scheme:dark;--bg:#191919;--panel:#262626;--text:#FBF4D7;--muted:#E7D7A1;--line:#FAF190;--accent:#ECCA5A;--accent2:#FAF190;--accent3:#FFFFFF;--panel-dark:#202020;}"
    "*{box-sizing:border-box}"
    "body{margin:0;min-height:100vh;padding:24px;font:16px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:var(--text);"
    "background:radial-gradient(circle at top,#262626,#191919 68%%);display:flex;align-items:center;justify-content:center}"
    ".card{width:min(700px,100%%);background:linear-gradient(180deg,rgba(38,38,38,.98),rgba(32,32,32,.98));border:1px solid var(--line);border-radius:24px;padding:28px;"
    "box-shadow:0 24px 60px rgba(0,0,0,.35)}"
    "h1{margin:0 0 10px;font-size:32px}"
    ".lead{margin:0 0 18px;color:var(--muted)}"
    "form{display:grid;gap:20px}"
    "label{display:grid;gap:8px;font-weight:600}"
    "input{width:100%%;border:1px solid #FAF190;border-radius:14px;background:#FBF4D7;color:#492304;padding:14px 16px;font:inherit}"
    "input::placeholder{color:#8C6A47}"
    ".hint{margin-top:-6px;color:var(--muted);font-size:13px;text-align:center}"
    "button{border:0;border-radius:14px;padding:14px 18px;font:inherit;font-weight:700;cursor:pointer;background:linear-gradient(135deg,var(--accent),var(--accent2) 62%%,var(--accent3));color:#492304;box-shadow:0 10px 24px rgba(236,202,90,.25)}"
    ".foot{margin-top:20px;color:var(--muted);font-size:13px;text-align:center}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='card'>"
    "<h1>Set Your Name</h1>"
    "<p class='lead'>Choose the name the storyteller will call you.</p>"
    "<form method='POST' action='/save'>"
    "<input type='text' name='name' maxlength='63' placeholder='Enter your name' value='%s'>"
    "<div class='hint'>Leave it empty to keep the default greeting<br>\"my little friend\".</div>"
    "</label>"
    "<button type='submit'>Save Name</button>"
    "</form>"
    "</div>"
    "</body>"
    "</html>";

static const char *kSuccessPage =
    "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Name Saved</title>"
    "<style>"
    "body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px;"
    "background:radial-gradient(circle at top,#262626,#191919 68%%);color:#FBF4D7;font:16px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
    ".card{max-width:520px;background:linear-gradient(180deg,rgba(38,38,38,.98),rgba(32,32,32,.98));border:1px solid #FAF190;border-radius:24px;padding:28px;box-shadow:0 24px 60px rgba(0,0,0,.35);text-align:center}"
    "h1{margin:0 0 10px;font-size:28px;color:#FAF190}"
    "p{margin:0;color:#E7D7A1}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='card'>"
    "<h1>Name Saved!</h1>"
    "<p>Return to the device and press OK to continue to the fairy tale.</p>"
    "</div>"
    "</body></html>";

static char *html_escape_dup(const char *src);
static void url_decode_inplace(char *value);
static bool form_get_value(const char *body, const char *key, char *out, size_t out_size);
static char *build_config_page(void);
static esp_err_t handle_root_get(httpd_req_t *req);
static esp_err_t handle_favicon_get(httpd_req_t *req);
static esp_err_t handle_save_post(httpd_req_t *req);
static esp_err_t handle_not_found(httpd_req_t *req, httpd_err_code_t err);
static esp_err_t start_http_server(void);
static void stop_dns_server(void);
static esp_err_t start_dns_server(void);
static void dns_server_task(void *arg);
static size_t dns_skip_question_name(const uint8_t *packet, size_t packet_len, size_t offset);
static void provisioning_stop_task(void *arg);
static void softap_provisioning_cleanup(void);
static void softap_provisioning_init_ssid(void);

static void softap_provisioning_init_ssid(void)
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_base_mac_addr_get(mac);
    if (err != ESP_OK) {
        err = esp_efuse_mac_get_default(mac);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read device MAC for SSID suffix: %s", esp_err_to_name(err));
        strlcpy(s_ap_ssid, AP_SSID_BASE, sizeof(s_ap_ssid));
        return;
    }

    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s-%02X%02X%02X",
             AP_SSID_BASE, mac[3], mac[4], mac[5]);
}

static char *html_escape_dup(const char *src)
{
    size_t len = 0;
    size_t cap = 0;
    size_t pos = 0;
    char *out = NULL;

    if (!src) {
        src = "";
    }

    len = strlen(src);
    cap = len * 6 + 1;
    out = malloc(cap);
    if (!out) {
        return NULL;
    }

    for (size_t i = 0; i < len; ++i) {
        const char *rep = NULL;
        switch (src[i]) {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        case '\'':
            rep = "&#39;";
            break;
        default:
            break;
        }

        if (rep) {
            size_t rep_len = strlen(rep);
            memcpy(out + pos, rep, rep_len);
            pos += rep_len;
        } else {
            out[pos++] = src[i];
        }
    }

    out[pos] = '\0';
    return out;
}

static void url_decode_inplace(char *value)
{
    char *src = value;
    char *dst = value;

    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
            continue;
        }

        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
            continue;
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}

static bool form_get_value(const char *body, const char *key, char *out, size_t out_size)
{
    size_t key_len = 0;
    const char *cursor = NULL;

    if (!body || !key || !out || out_size == 0) {
        return false;
    }

    key_len = strlen(key);
    cursor = body;

    while (*cursor) {
        const char *pair_end = strchr(cursor, '&');
        const char *value_sep = strchr(cursor, '=');
        size_t value_len = 0;

        if (!pair_end) {
            pair_end = cursor + strlen(cursor);
        }

        if (!value_sep || value_sep > pair_end) {
            cursor = (*pair_end == '&') ? pair_end + 1 : pair_end;
            continue;
        }

        if ((size_t)(value_sep - cursor) == key_len && strncmp(cursor, key, key_len) == 0) {
            value_len = (size_t)(pair_end - value_sep - 1);
            if (value_len >= out_size) {
                value_len = out_size - 1;
            }
            memcpy(out, value_sep + 1, value_len);
            out[value_len] = '\0';
            url_decode_inplace(out);
            return true;
        }

        cursor = (*pair_end == '&') ? pair_end + 1 : pair_end;
    }

    out[0] = '\0';
    return false;
}

static char *build_config_page(void)
{
    char name[SETTINGS_STORE_USER_NAME_MAX_LEN + 1] = {0};
    bool found = false;
    char *esc_name = NULL;
    char *html = NULL;
    size_t cap = 0;

    if (settings_store_get_user_name(name, sizeof(name), &found) != ESP_OK || !found) {
        name[0] = '\0';
    }

    esc_name = html_escape_dup(name);
    if (!esc_name) {
        return NULL;
    }

    cap = strlen(kConfigPageTemplate) + strlen(esc_name) + 64;
    html = malloc(cap);
    if (!html) {
        free(esc_name);
        return NULL;
    }

    snprintf(html, cap, kConfigPageTemplate, esc_name);
    free(esc_name);
    return html;
}

static esp_err_t handle_root_get(httpd_req_t *req)
{
    char *html = build_config_page();

    if (!html) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to build page");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return ESP_OK;
}

static esp_err_t handle_favicon_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t handle_save_post(httpd_req_t *req)
{
    char *body = NULL;
    char name[SETTINGS_STORE_USER_NAME_MAX_LEN + 1] = {0};
    int remaining = 0;
    int offset = 0;
    esp_err_t err = ESP_OK;

    if (req->content_len <= 0 || req->content_len > MAX_FORM_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form body");
        return ESP_FAIL;
    }

    body = malloc((size_t)req->content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, body + offset, remaining);
        if (received <= 0) {
            free(body);
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
                return ESP_ERR_TIMEOUT;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        offset += received;
        remaining -= received;
    }
    body[offset] = '\0';

    form_get_value(body, "name", name, sizeof(name));
    free(body);

    err = settings_store_set_user_name(name);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save name");
        return err;
    }

    ESP_LOGI(TAG, "Saved user name via captive portal: '%s'", name);
    s_name_saved = true;

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, kSuccessPage);

    if (xTaskCreate(provisioning_stop_task, "prov_stop", PROVISIONING_STOP_TASK_STACK, NULL, 10, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Failed to schedule portal stop task");
    }

    return ESP_OK;
}

static esp_err_t handle_not_found(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_root_get,
        .user_ctx = NULL,
    };
    httpd_uri_t generate_204 = {
        .uri = "/generate_204",
        .method = HTTP_GET,
        .handler = handle_root_get,
        .user_ctx = NULL,
    };
    httpd_uri_t fwlink = {
        .uri = "/fwlink",
        .method = HTTP_GET,
        .handler = handle_root_get,
        .user_ctx = NULL,
    };
    httpd_uri_t hotspot = {
        .uri = "/hotspot-detect.html",
        .method = HTTP_GET,
        .handler = handle_root_get,
        .user_ctx = NULL,
    };
    httpd_uri_t connectivity = {
        .uri = "/connectivity-check.html",
        .method = HTTP_GET,
        .handler = handle_root_get,
        .user_ctx = NULL,
    };
    httpd_uri_t favicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = handle_favicon_get,
        .user_ctx = NULL,
    };
    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = handle_save_post,
        .user_ctx = NULL,
    };

    config.stack_size = 8192;
    config.server_port = HTTP_PORT;
    config.max_uri_handlers = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_RETURN_ON_ERROR(httpd_start(&s_http_server, &config), TAG, "httpd_start failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &root), TAG, "register root failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &generate_204), TAG, "register generate_204 failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &fwlink), TAG, "register fwlink failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &hotspot), TAG, "register hotspot failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &connectivity), TAG, "register connectivity failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &favicon), TAG, "register favicon failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &save), TAG, "register save failed");
    httpd_register_err_handler(s_http_server, HTTPD_404_NOT_FOUND, handle_not_found);
    return ESP_OK;
}

static void stop_dns_server(void)
{
    s_dns_running = false;

    if (s_dns_sock >= 0) {
        shutdown(s_dns_sock, SHUT_RDWR);
        close(s_dns_sock);
        s_dns_sock = -1;
    }
}

static size_t dns_skip_question_name(const uint8_t *packet, size_t packet_len, size_t offset)
{
    while (offset < packet_len) {
        uint8_t len = packet[offset];
        if (len == 0) {
            return offset + 1;
        }
        if ((len & 0xC0) == 0xC0) {
            return offset + 2;
        }
        offset += (size_t)len + 1;
    }
    return packet_len;
}

static void dns_server_task(void *arg)
{
    uint8_t rx[512];
    uint8_t tx[512];
    struct sockaddr_in bind_addr = {0};
    uint32_t ap_ip_addr = inet_addr(s_ap_url + strlen("http://"));

    (void)arg;

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(DNS_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s_dns_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        close(s_dns_sock);
        s_dns_sock = -1;
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS redirect server started on port %d", DNS_PORT);

    while (s_dns_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        ssize_t rx_len = recvfrom(s_dns_sock, rx, sizeof(rx), 0, (struct sockaddr *)&client_addr, &client_len);

        if (rx_len <= 0 || rx_len < 12) {
            continue;
        }

        size_t qname_end = dns_skip_question_name(rx, (size_t)rx_len, 12);
        if (qname_end + 4 > (size_t)rx_len) {
            continue;
        }

        uint16_t qtype = (uint16_t)((rx[qname_end] << 8) | rx[qname_end + 1]);
        size_t question_len = qname_end + 4 - 12;
        size_t tx_len = 0;
        bool answer_a = (qtype == 1 || qtype == 255);

        memcpy(tx, rx, 2);
        tx[2] = 0x81;
        tx[3] = 0x80;
        tx[4] = 0x00;
        tx[5] = 0x01;
        tx[6] = 0x00;
        tx[7] = answer_a ? 0x01 : 0x00;
        tx[8] = 0x00;
        tx[9] = 0x00;
        tx[10] = 0x00;
        tx[11] = 0x00;
        tx_len = 12;

        memcpy(tx + tx_len, rx + 12, question_len);
        tx_len += question_len;

        if (answer_a) {
            tx[tx_len++] = 0xC0;
            tx[tx_len++] = 0x0C;
            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x01;
            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x01;
            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x3C;
            tx[tx_len++] = 0x00;
            tx[tx_len++] = 0x04;
            memcpy(tx + tx_len, &ap_ip_addr, 4);
            tx_len += 4;
        }

        sendto(s_dns_sock, tx, tx_len, 0, (struct sockaddr *)&client_addr, client_len);
    }

    if (s_dns_sock >= 0) {
        close(s_dns_sock);
        s_dns_sock = -1;
    }

    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t start_dns_server(void)
{
    if (s_dns_task) {
        return ESP_OK;
    }

    s_dns_running = true;
    if (xTaskCreate(dns_server_task, "prov_dns", PROVISIONING_DNS_TASK_STACK, NULL, 5, &s_dns_task) != pdPASS) {
        s_dns_running = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void provisioning_stop_task(void *arg)
{
    (void)arg;
    softap_provisioning_stop();
    vTaskDelete(NULL);
}

static void softap_provisioning_cleanup(void)
{
    if (s_http_server) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
    }

    stop_dns_server();
    while (s_dns_task) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_stop failed during cleanup: %s", esp_err_to_name(err));
    }

    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }

    strlcpy(s_ap_url, AP_DEFAULT_URL, sizeof(s_ap_url));
    s_portal_running = false;
}

esp_err_t softap_provisioning_init(void)
{
    softap_provisioning_init_ssid();
    return ESP_OK;
}

esp_err_t softap_provisioning_start(void)
{
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t ap_cfg = {0};
    esp_netif_ip_info_t ip_info = {0};
    esp_err_t err = ESP_OK;
    size_t ssid_len = 0;

    if (s_portal_running) {
        ESP_LOGW(TAG, "Provisioning portal already running");
        return ESP_OK;
    }

    if (s_portal_stopping) {
        return ESP_ERR_INVALID_STATE;
    }

    s_name_saved = false;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }

    err = esp_wifi_init(&wifi_init_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set storage failed: %s", esp_err_to_name(err));
        goto fail;
    }

    ssid_len = strnlen(s_ap_ssid, sizeof(ap_cfg.ap.ssid));
    memcpy(ap_cfg.ap.ssid, s_ap_ssid, ssid_len);
    strncpy((char *)ap_cfg.ap.password, AP_PASSWORD, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.ssid_len = ssid_len;
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set mode failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set AP config failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(err));
        goto fail;
    }

    if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
        snprintf(s_ap_url, sizeof(s_ap_url), "http://" IPSTR, IP2STR(&ip_info.ip));
    } else {
        strlcpy(s_ap_url, AP_DEFAULT_URL, sizeof(s_ap_url));
    }

    err = start_http_server();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = start_dns_server();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DNS server start failed: %s", esp_err_to_name(err));
        goto fail;
    }

    s_portal_running = true;
    ESP_LOGI(TAG, "SoftAP provisioning started: SSID=%s URL=%s", s_ap_ssid, s_ap_url);
    return ESP_OK;

fail:
    s_portal_stopping = true;
    softap_provisioning_cleanup();
    s_portal_stopping = false;
    return err;
}

void softap_provisioning_stop(void)
{
    if (!s_portal_running && !s_http_server && !s_dns_task && !s_ap_netif) {
        return;
    }

    if (s_portal_stopping) {
        while (s_portal_stopping) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return;
    }

    s_portal_stopping = true;
    ESP_LOGI(TAG, "Stopping SoftAP provisioning");

    softap_provisioning_cleanup();
    s_portal_stopping = false;
    ESP_LOGI(TAG, "SoftAP provisioning stopped");
}

bool softap_provisioning_is_running(void)
{
    return s_portal_running;
}

bool softap_provisioning_is_stopping(void)
{
    return s_portal_stopping;
}

bool softap_provisioning_has_saved_name(void)
{
    return s_name_saved;
}

const char *softap_provisioning_get_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *softap_provisioning_get_ap_password(void)
{
    return AP_PASSWORD;
}

const char *softap_provisioning_get_ap_url(void)
{
    return s_ap_url;
}
