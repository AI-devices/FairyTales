#include "ui_provision_screen.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "softap_provisioning.h"
#include "story_audio.h"
#include "ui_img_manager.h"

LV_FONT_DECLARE(ui_font_Font5);
LV_FONT_DECLARE(ui_font_Font41);
LV_FONT_DECLARE(ui_font_Font51);
LV_FONT_DECLARE(ui_font_Font52);

typedef enum
{
    UI_PROVISION_DECOR_ASSET_STAR_MID = 0,
    UI_PROVISION_DECOR_ASSET_STAR_MINI,
    UI_PROVISION_DECOR_ASSET_SPARK_BIG,
    UI_PROVISION_DECOR_ASSET_SPARK_MID,
    UI_PROVISION_DECOR_ASSET_SPARK_MINI,
    UI_PROVISION_DECOR_ASSET_COUNT
} ui_provision_decor_asset_id_t;

typedef struct
{
    const char *spiffs_path;
    uint32_t raw_size;
    uint8_t *blob;
    lv_img_dsc_t img_dsc;
} ui_provision_decor_asset_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *wifi_label;
    lv_obj_t *url_label;
    lv_obj_t *wifi_qr;
    lv_obj_t *url_qr;
    lv_obj_t *ok_btn;
    lv_obj_t *ok_btn_label;
    lv_timer_t *poll_timer;

    volatile bool stop_task_running;
    bool continue_pending;
    bool name_saved_seen;
    bool save_chime_played;
    char last_url[32];

    ui_provision_screen_callbacks_t callbacks;
} ui_provision_screen_state_t;

static const char *TAG = "ui_provision";
static const uint32_t UI_PROVISION_STOP_TASK_STACK = 8192;
static ui_provision_screen_state_t s_state = {0};
static ui_provision_decor_asset_t s_decor_assets[UI_PROVISION_DECOR_ASSET_COUNT] = {
    [UI_PROVISION_DECOR_ASSET_STAR_MID] = {
        .spiffs_path = "S:/assets/screen_stars/star_mid.bin",
        .raw_size = 7504U,
    },
    [UI_PROVISION_DECOR_ASSET_STAR_MINI] = {
        .spiffs_path = "S:/assets/screen_stars/star_mini.bin",
        .raw_size = 2704U,
    },
    [UI_PROVISION_DECOR_ASSET_SPARK_BIG] = {
        .spiffs_path = "S:/assets/screen_stars/spark_big.bin",
        .raw_size = 7504U,
    },
    [UI_PROVISION_DECOR_ASSET_SPARK_MID] = {
        .spiffs_path = "S:/assets/screen_stars/spark_mid.bin",
        .raw_size = 2704U,
    },
    [UI_PROVISION_DECOR_ASSET_SPARK_MINI] = {
        .spiffs_path = "S:/assets/screen_stars/spark_mini.bin",
        .raw_size = 1204U,
    },
};

static void ui_provision_screen_root_delete_event_cb(lv_event_t *event);
static void ui_provision_screen_poll_timer_cb(lv_timer_t *timer);
static void ui_provision_screen_ok_event_cb(lv_event_t *event);
static void ui_provision_screen_stop_task(void *arg);
static void ui_provision_screen_refresh_qr_content(void);
static void ui_provision_screen_refresh_controls(void);
static void ui_provision_screen_maybe_continue(void);
static void ui_provision_screen_set_ok_accepted(bool accepted);
static void ui_provision_screen_style_yellow_button_pressed_shadow(lv_obj_t *button);
static void ui_provision_screen_create_decorations(lv_obj_t *parent);
static void ui_provision_screen_release_decor_assets(void);
static lv_obj_t *ui_provision_screen_create_step_column(lv_obj_t *parent,
                                                        lv_coord_t label_width,
                                                        const char *initial_text,
                                                        lv_obj_t **label_out,
                                                        lv_obj_t **qr_out);
static const lv_img_dsc_t *ui_provision_screen_get_decor_asset(ui_provision_decor_asset_id_t asset_id);

static lv_obj_t *ui_provision_screen_create_glow_dot(lv_obj_t *parent,
                                                     lv_coord_t x,
                                                     lv_coord_t y,
                                                     lv_coord_t w,
                                                     lv_coord_t h,
                                                     lv_coord_t spread)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, w, h);
    lv_obj_set_x(dot, x);
    lv_obj_set_y(dot, y);
    lv_obj_set_align(dot, LV_ALIGN_CENTER);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dot, 100, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xFAF190), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_shadow_color(dot, lv_color_hex(0xFAF190), 0);
    lv_obj_set_style_shadow_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(dot, 40, 0);
    lv_obj_set_style_shadow_spread(dot, spread, 0);
    return dot;
}

static lv_obj_t *ui_provision_screen_create_img(lv_obj_t *parent,
                                                const lv_img_dsc_t *src,
                                                lv_coord_t x,
                                                lv_coord_t y,
                                                int16_t angle)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, src);
    lv_obj_set_x(img, x);
    lv_obj_set_y(img, y);
    lv_obj_set_align(img, LV_ALIGN_CENTER);
    lv_obj_add_flag(img, LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    if (angle != 0) {
        lv_img_set_angle(img, angle);
    }
    return img;
}

static bool ui_provision_screen_load_decor_asset(ui_provision_decor_asset_t *asset)
{
    lv_img_header_t header = {0};

    if (!asset) {
        return false;
    }

    if (asset->blob) {
        return true;
    }

    asset->blob = UI_LOAD_IMAGE(asset->spiffs_path, asset->raw_size);
    if (!asset->blob) {
        return false;
    }

    if (asset->raw_size <= sizeof(header)) {
        heap_caps_free(asset->blob);
        asset->blob = NULL;
        return false;
    }

    memcpy(&header, asset->blob, sizeof(header));

    memset(&asset->img_dsc, 0, sizeof(asset->img_dsc));
    asset->img_dsc.header = header;
    asset->img_dsc.data = asset->blob + sizeof(header);
    asset->img_dsc.data_size = asset->raw_size - sizeof(header);

    return true;
}

static const lv_img_dsc_t *ui_provision_screen_get_decor_asset(ui_provision_decor_asset_id_t asset_id)
{
    ui_provision_decor_asset_t *asset = NULL;

    if ((unsigned)asset_id >= UI_PROVISION_DECOR_ASSET_COUNT) {
        return NULL;
    }

    asset = &s_decor_assets[asset_id];
    if (!ui_provision_screen_load_decor_asset(asset)) {
        return NULL;
    }

    return &asset->img_dsc;
}

static void ui_provision_screen_release_decor_assets(void)
{
    size_t i = 0;

    for (i = 0; i < UI_PROVISION_DECOR_ASSET_COUNT; ++i) {
        if (s_decor_assets[i].blob) {
            heap_caps_free(s_decor_assets[i].blob);
            s_decor_assets[i].blob = NULL;
        }

        memset(&s_decor_assets[i].img_dsc, 0, sizeof(s_decor_assets[i].img_dsc));
    }
}

static void ui_provision_screen_root_delete_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);

    if (softap_provisioning_is_running() || softap_provisioning_is_stopping()) {
        softap_provisioning_stop();
    }

    if (s_state.poll_timer) {
        lv_timer_del(s_state.poll_timer);
        s_state.poll_timer = NULL;
    }

    s_state.root = NULL;
    s_state.wifi_label = NULL;
    s_state.url_label = NULL;
    s_state.wifi_qr = NULL;
    s_state.url_qr = NULL;
    s_state.ok_btn = NULL;
    s_state.ok_btn_label = NULL;
    s_state.continue_pending = false;
    s_state.stop_task_running = false;
    s_state.last_url[0] = '\0';
    ui_provision_screen_release_decor_assets();
    memset(&s_state.callbacks, 0, sizeof(s_state.callbacks));
}

static void ui_provision_screen_refresh_qr_content(void)
{
    char wifi_payload[128];
    const char *ssid = softap_provisioning_get_ap_ssid();
    const char *url = softap_provisioning_get_ap_url();

    if (!ssid) {
        ssid = "";
    }
    if (!url) {
        url = "";
    }

    if (s_state.wifi_label) {
        lv_label_set_text_fmt(s_state.wifi_label, "1. Connect to %s", ssid);
    }

    if (s_state.url_label) {
        lv_label_set_text_fmt(s_state.url_label, "2. Open setup page %s", url);
    }

    if (strcmp(s_state.last_url, url) == 0) {
        return;
    }

    strlcpy(s_state.last_url, url, sizeof(s_state.last_url));
    snprintf(wifi_payload, sizeof(wifi_payload), "WIFI:T:WPA;S:%s;P:%s;;",
             ssid,
             softap_provisioning_get_ap_password());

#if LV_USE_QRCODE
    if (s_state.wifi_qr) {
        lv_qrcode_update(s_state.wifi_qr, wifi_payload, strlen(wifi_payload));
        lv_obj_center(s_state.wifi_qr);
    }

    if (s_state.url_qr) {
        lv_qrcode_update(s_state.url_qr, url, strlen(url));
        lv_obj_center(s_state.url_qr);
    }
#endif
}

static void ui_provision_screen_refresh_controls(void)
{
    bool waiting = s_state.continue_pending || softap_provisioning_is_stopping() || s_state.stop_task_running;

    if (s_state.ok_btn) {
        if (waiting) {
            lv_obj_add_state(s_state.ok_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_state.ok_btn, LV_STATE_DISABLED);
        }
    }
}

static void ui_provision_screen_set_ok_accepted(bool accepted)
{
    if (!s_state.ok_btn) {
        return;
    }

    lv_obj_set_style_shadow_color(s_state.ok_btn, lv_color_hex(0xECCA5A), 0);
    lv_obj_set_style_shadow_opa(s_state.ok_btn, accepted ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_state.ok_btn, accepted ? 30 : 0, 0);
    lv_obj_set_style_shadow_spread(s_state.ok_btn, accepted ? 2 : 0, 0);
}

static void ui_provision_screen_style_yellow_button_pressed_shadow(lv_obj_t *button)
{
    if (!button) {
        return;
    }

    lv_obj_set_style_shadow_color(button, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 30, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(button, 2, LV_PART_MAIN | LV_STATE_PRESSED);
}

static void ui_provision_screen_maybe_continue(void)
{
    if (!s_state.continue_pending) {
        return;
    }

    if (softap_provisioning_is_running() || softap_provisioning_is_stopping() || s_state.stop_task_running) {
        return;
    }

    s_state.continue_pending = false;
    ui_provision_screen_refresh_controls();

    if (s_state.callbacks.continue_requested) {
        s_state.callbacks.continue_requested(s_state.callbacks.user_ctx);
    }
}

static void ui_provision_screen_poll_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (softap_provisioning_has_saved_name() && !s_state.name_saved_seen) {
        s_state.name_saved_seen = true;
        ui_provision_screen_set_ok_accepted(true);
        if (!s_state.save_chime_played) {
            (void)story_audio_play_cue(STORY_AUDIO_CUE_SUCCESS_CHIME);
            s_state.save_chime_played = true;
        }
    }

    ui_provision_screen_refresh_qr_content();
    ui_provision_screen_refresh_controls();
    ui_provision_screen_maybe_continue();
}

static void ui_provision_screen_stop_task(void *arg)
{
    (void)arg;
    softap_provisioning_stop();
    s_state.stop_task_running = false;
    vTaskDelete(NULL);
}

static void ui_provision_screen_ok_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED || s_state.continue_pending) {
        return;
    }

    if (!softap_provisioning_is_running() && !softap_provisioning_is_stopping()) {
        if (s_state.callbacks.continue_requested) {
            s_state.callbacks.continue_requested(s_state.callbacks.user_ctx);
        }
        return;
    }

    s_state.continue_pending = true;

    if (!s_state.stop_task_running) {
        s_state.stop_task_running = true;
        if (xTaskCreate(ui_provision_screen_stop_task, "prov_ui_ok_stop", UI_PROVISION_STOP_TASK_STACK, NULL, 10, NULL) != pdPASS) {
            s_state.stop_task_running = false;
            s_state.continue_pending = false;
            ESP_LOGE(TAG, "Failed to create stop task");
        }
    }

    ui_provision_screen_refresh_controls();
}

static lv_obj_t *ui_provision_screen_create_step_column(lv_obj_t *parent,
                                                        lv_coord_t label_width,
                                                        const char *initial_text,
                                                        lv_obj_t **label_out,
                                                        lv_obj_t **qr_out)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, 314, 294);
    lv_obj_set_align(container, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    *label_out = lv_label_create(container);
    lv_obj_set_width(*label_out, label_width);
    lv_label_set_text(*label_out, initial_text);
    lv_label_set_long_mode(*label_out, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(*label_out, lv_color_hex(0xECCA5A), 0);
    lv_obj_set_style_text_opa(*label_out, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(*label_out, 0, 0);
    lv_obj_set_style_text_line_space(*label_out, 5, 0);
    lv_obj_set_style_text_align(*label_out, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(*label_out, &ui_font_Font51, 0);

    lv_obj_t *outer = lv_obj_create(container);
    lv_obj_set_size(outer, 210, 210);
    lv_obj_set_align(outer, LV_ALIGN_CENTER);
    lv_obj_clear_flag(outer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(outer, 10, 0);
    lv_obj_set_style_bg_color(outer, lv_color_hex(0xFAF190), 0);
    lv_obj_set_style_bg_opa(outer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(outer, 0, 0);
    lv_obj_set_style_shadow_width(outer, 0, 0);
    lv_obj_set_style_pad_all(outer, 0, 0);

    lv_obj_t *host = lv_obj_create(outer);
    lv_obj_set_size(host, 185, 185);
    lv_obj_set_align(host, LV_ALIGN_CENTER);
    lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(host, 0, 0);
    lv_obj_set_style_bg_color(host, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(host, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(host, 0, 0);
    lv_obj_set_style_shadow_width(host, 0, 0);
    lv_obj_set_style_pad_all(host, 0, 0);

#if LV_USE_QRCODE
    *qr_out = lv_qrcode_create(host, 185, lv_color_hex(0x171717), lv_color_white());
    if (*qr_out) {
        lv_obj_center(*qr_out);
    }
#else
    *qr_out = lv_label_create(host);
    lv_label_set_text(*qr_out, "QR");
    lv_obj_center(*qr_out);
    lv_obj_set_style_text_color(*qr_out, lv_color_hex(0x171717), 0);
    lv_obj_set_style_text_font(*qr_out, &ui_font_Font51, 0);
#endif

    return container;
}

static void ui_provision_screen_create_decorations(lv_obj_t *parent)
{
    const lv_img_dsc_t *star_mid = ui_provision_screen_get_decor_asset(UI_PROVISION_DECOR_ASSET_STAR_MID);
    const lv_img_dsc_t *star_mini = ui_provision_screen_get_decor_asset(UI_PROVISION_DECOR_ASSET_STAR_MINI);
    const lv_img_dsc_t *spark_big = ui_provision_screen_get_decor_asset(UI_PROVISION_DECOR_ASSET_SPARK_BIG);
    const lv_img_dsc_t *spark_mid = ui_provision_screen_get_decor_asset(UI_PROVISION_DECOR_ASSET_SPARK_MID);
    const lv_img_dsc_t *spark_mini = ui_provision_screen_get_decor_asset(UI_PROVISION_DECOR_ASSET_SPARK_MINI);

    ui_provision_screen_create_glow_dot(parent, -393, -252, 10, 10, 9);
    if (star_mid) {
        ui_provision_screen_create_img(parent, star_mid, -393, -254, 100);
    }

    ui_provision_screen_create_glow_dot(parent, -391, 66, 4, 5, 7);
    if (star_mini) {
        ui_provision_screen_create_img(parent, star_mini, -391, 66, 9);
    }
    if (spark_big) {
        ui_provision_screen_create_img(parent, spark_big, -470, 9, -90);
        ui_provision_screen_create_img(parent, spark_big, 437, 79, 120);
    }
    if (spark_mid) {
        ui_provision_screen_create_img(parent, spark_mid, -375, -173, -90);
        ui_provision_screen_create_img(parent, spark_mid, -461, 127, 90);
        ui_provision_screen_create_img(parent, spark_mid, 465, -58, 90);
    }
    if (spark_mini) {
        ui_provision_screen_create_img(parent, spark_mini, -404, -62, 0);
        ui_provision_screen_create_img(parent, spark_mini, 406, -214, 0);
        ui_provision_screen_create_img(parent, spark_mini, 407, 126, 0);
    }

    ui_provision_screen_create_glow_dot(parent, -461, -123, 5, 5, 7);
    if (star_mini) {
        ui_provision_screen_create_img(parent, star_mini, -459, -124, 9);
    }

    ui_provision_screen_create_glow_dot(parent, 441, -124, 10, 10, 9);
    if (star_mid) {
        ui_provision_screen_create_img(parent, star_mid, 442, -127, 100);
    }

    ui_provision_screen_create_glow_dot(parent, 473, -256, 4, 5, 7);
    if (star_mini) {
        ui_provision_screen_create_img(parent, star_mini, 473, -256, 9);
    }

    ui_provision_screen_create_glow_dot(parent, 395, 4, 4, 5, 7);
    if (star_mini) {
        ui_provision_screen_create_img(parent, star_mini, 395, 3, 9);
    }
}

void ui_provision_screen_register_callbacks(const ui_provision_screen_callbacks_t *callbacks)
{
    if (!callbacks) {
        memset(&s_state.callbacks, 0, sizeof(s_state.callbacks));
        return;
    }

    s_state.callbacks = *callbacks;
}

void ui_provision_screen_init(void)
{
    if (s_state.root) {
        return;
    }

    if (!softap_provisioning_is_running()) {
        ESP_LOGW(TAG, "Provision screen opened before SoftAP reached running state");
    }

    s_state.name_saved_seen = softap_provisioning_has_saved_name();
    s_state.save_chime_played = false;

    s_state.root = lv_obj_create(NULL);
    lv_obj_add_event_cb(s_state.root, ui_provision_screen_root_delete_event_cb, LV_EVENT_DELETE, NULL);
    lv_obj_clear_flag(s_state.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_state.root, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_state.root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(s_state.root, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_state.root, 50, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_state.root, 0, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(s_state.root);
    lv_obj_set_x(title, 0);
    lv_obj_set_y(title, -257);
    lv_obj_set_align(title, LV_ALIGN_CENTER);
    lv_label_set_text(title, "Set Your Name");
    lv_obj_add_flag(title, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xECCA5A), 0);
    lv_obj_set_style_text_opa(title, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(title, &ui_font_Font5, 0);

    lv_obj_t *subtitle = lv_label_create(s_state.root);
    lv_obj_set_width(subtitle, 671);
    lv_obj_set_x(subtitle, -12);
    lv_obj_set_y(subtitle, -160);
    lv_obj_set_align(subtitle, LV_ALIGN_CENTER);
    lv_label_set_text(subtitle,
                      "Use your phone to open the captive portal and choose the name the storyteller will call you\n"
                      "or\n"
                      "press OK to continue with default \"my little friend\"");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xFAF190), 0);
    lv_obj_set_style_text_opa(subtitle, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(subtitle, 0, 0);
    lv_obj_set_style_text_line_space(subtitle, 3, 0);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(subtitle, &ui_font_Font52, 0);

    ui_provision_screen_create_decorations(s_state.root);

    lv_obj_t *content = lv_obj_create(s_state.root);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 704, 357);
    lv_obj_set_x(content, 0);
    lv_obj_set_y(content, 72);
    lv_obj_set_align(content, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(content, 0, 0);
    lv_obj_set_style_pad_column(content, 80, 0);

    ui_provision_screen_create_step_column(content, 280, "1. Connect to", &s_state.wifi_label, &s_state.wifi_qr);
    ui_provision_screen_create_step_column(content, 241, "2. Open setup page", &s_state.url_label, &s_state.url_qr);

    s_state.ok_btn = lv_btn_create(s_state.root);
    lv_obj_set_size(s_state.ok_btn, 132, 69);
    lv_obj_set_x(s_state.ok_btn, 0);
    lv_obj_set_y(s_state.ok_btn, 246);
    lv_obj_set_align(s_state.ok_btn, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(s_state.ok_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_state.ok_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_state.ok_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(s_state.ok_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_state.ok_btn, 100, 0);
    lv_obj_set_style_bg_color(s_state.ok_btn, lv_color_hex(0xFAF190), 0);
    lv_obj_set_style_bg_opa(s_state.ok_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_state.ok_btn, 0, 0);
    ui_provision_screen_set_ok_accepted(false);
    ui_provision_screen_style_yellow_button_pressed_shadow(s_state.ok_btn);
    lv_obj_add_event_cb(s_state.ok_btn, ui_provision_screen_ok_event_cb, LV_EVENT_CLICKED, NULL);

    s_state.ok_btn_label = lv_label_create(s_state.ok_btn);
    lv_obj_set_align(s_state.ok_btn_label, LV_ALIGN_CENTER);
    lv_label_set_text(s_state.ok_btn_label, "Ok");
    lv_label_set_recolor(s_state.ok_btn_label, true);
    lv_obj_set_style_text_color(s_state.ok_btn_label, lv_color_hex(0x171717), 0);
    lv_obj_set_style_text_opa(s_state.ok_btn_label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(s_state.ok_btn_label, &ui_font_Font41, 0);

    s_state.poll_timer = lv_timer_create(ui_provision_screen_poll_timer_cb, 200, NULL);
    s_state.last_url[0] = '\0';
    s_state.continue_pending = false;
    s_state.name_saved_seen = false;
    s_state.save_chime_played = false;
    s_state.stop_task_running = false;
    ui_provision_screen_refresh_qr_content();
    ui_provision_screen_refresh_controls();
}

lv_obj_t *ui_provision_screen_root(void)
{
    return s_state.root;
}

void ui_provision_screen_destroy(void)
{
    if (softap_provisioning_is_running() || softap_provisioning_is_stopping()) {
        softap_provisioning_stop();
    }

    if (s_state.root) {
        lv_obj_del(s_state.root);
    }
}
