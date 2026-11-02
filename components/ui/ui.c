#include "ui.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "audio_session.h"
#include "settings_store.h"
#include "story_audio.h"
#include "story_coordinator.h"
#include "ui_TitleScreen.h"
#include "ui_dual_gif_screen.h"

#if defined __has_include
#if __has_include("lvgl.h")
#include "lvgl.h"
#elif __has_include("lvgl/lvgl.h")
#include "lvgl/lvgl.h"
#else
#include "lvgl.h"
#endif
#else
#include "lvgl.h"
#endif

#if LV_COLOR_DEPTH != 16
#error "LV_COLOR_DEPTH should be 16bit to match the UI asset format"
#endif

#if LV_COLOR_16_SWAP != 0
#error "LV_COLOR_16_SWAP should be 0 to match the UI asset format"
#endif

#define UI_TITLESCREEN_TOTAL_MS 5000U
#define UI_TITLESCREEN_FADE_OUT_MS 700U
#define UI_MAINSCREEN_FADE_IN_MS 700U
/* Touch-controller latency makes a 1 s software timer feel like the
 * user-facing 2 s hold described by the UI and README. */
#define UI_MODE_HOLD_MS 1000U
#define UI_MODE_HOTSPOT_SIZE 128
#define UI_MODE_LOCK_TIMEOUT_MS 1U
#define UI_MODE_LOCK_RETRY_US 10000U

static const char *TAG = "ui";

typedef enum
{
    UI_STAGE_NONE = 0,
    UI_STAGE_TITLE,
    UI_STAGE_STORY,
} ui_stage_t;

static lv_timer_t *s_title_transition_timer = NULL;
static lv_timer_t *s_stage_prepare_timer = NULL;
static lv_obj_t *s_transition_overlay = NULL;
static bool s_story_started = false;
static bool s_story_reset_pending = false;
static ui_stage_t s_current_stage = UI_STAGE_NONE;
static ui_stage_t s_pending_stage = UI_STAGE_NONE;
static lv_obj_t *s_mode_hotspot = NULL;
static lv_obj_t *s_mode_overlay = NULL;
static lv_obj_t *s_mode_hint_overlay = NULL;
static lv_obj_t *s_mode_hint_checkbox = NULL;
static bool s_mode_hint_preference_loaded = false;
static bool s_mode_hint_pending = false;
static esp_timer_handle_t s_mode_hold_timer = NULL;
static volatile bool s_mode_hold_active = false;
static int64_t s_mode_hold_started_us = 0;

static void ui_cancel_title_transition_timer(void);
static void ui_title_transition_timer_cb(lv_timer_t *timer);
static bool ui_show_mode_hint(void);

static void ui_start_title_sequence(void)
{
    ui_cancel_title_transition_timer();
    s_story_started = false;
    ui_dual_gif_screen_preload_initial_media();
    ui_TitleScreen_start_intro_animation();
    s_title_transition_timer = lv_timer_create(ui_title_transition_timer_cb, UI_TITLESCREEN_TOTAL_MS, NULL);
    if (s_title_transition_timer) {
        lv_timer_set_repeat_count(s_title_transition_timer, 1);
    } else {
        ui_title_transition_timer_cb(NULL);
    }
}

static void ui_cancel_title_transition_timer(void)
{
    if (s_title_transition_timer) {
        lv_timer_del(s_title_transition_timer);
        s_title_transition_timer = NULL;
    }
}

static void ui_cancel_stage_prepare_timer(void)
{
    if (s_stage_prepare_timer) {
        lv_timer_del(s_stage_prepare_timer);
        s_stage_prepare_timer = NULL;
    }
}

static void ui_transition_overlay_set_opa_cb(void *obj, int32_t value)
{
    if (!obj) {
        return;
    }

    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static lv_obj_t *ui_ensure_transition_overlay(void)
{
    lv_obj_t *parent = NULL;

    if (s_transition_overlay) {
        return s_transition_overlay;
    }

    parent = lv_layer_top();
    if (!parent) {
        return NULL;
    }

    s_transition_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_transition_overlay);
    lv_obj_set_size(s_transition_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_align(s_transition_overlay, LV_ALIGN_CENTER);
    lv_obj_clear_flag(s_transition_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_transition_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_transition_overlay, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_transition_overlay, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_opa(s_transition_overlay, LV_OPA_TRANSP, 0);
    lv_obj_move_foreground(s_transition_overlay);

    return s_transition_overlay;
}

static void ui_reset_transition_overlay(void)
{
    if (!s_transition_overlay) {
        return;
    }

    lv_anim_del(s_transition_overlay, ui_transition_overlay_set_opa_cb);
    ui_transition_overlay_set_opa_cb(s_transition_overlay, LV_OPA_TRANSP);
    lv_obj_add_flag(s_transition_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void ui_story_start_if_needed(void)
{
    if (s_story_started) {
        return;
    }

    s_story_started = true;
    story_coordinator_start();
}

static void ui_story_stage_ready(void)
{
    if (s_mode_hint_pending) {
        s_mode_hint_pending = false;
        if (ui_show_mode_hint()) {
            return;
        }
    }
    ui_story_start_if_needed();
}

static void ui_transition_fade_in_ready_cb(lv_anim_t *anim)
{
    LV_UNUSED(anim);

    ui_reset_transition_overlay();
    if (s_current_stage == UI_STAGE_STORY) {
        ui_story_stage_ready();
    }
}

static esp_err_t ui_prepare_stage(ui_stage_t stage);
static void ui_stage_prepare_timer_cb(lv_timer_t *timer);
static void ui_finish_stage_transition(void);

static lv_obj_t *ui_create_stage_screen(ui_stage_t stage)
{
    switch (stage) {
    case UI_STAGE_TITLE:
        ui_TitleScreen_screen_init();
        return ui_TitleScreen;
    case UI_STAGE_STORY:
        ui_dual_gif_screen_init();
        return ui_dual_gif_screen_root();
    case UI_STAGE_NONE:
    default:
        return NULL;
    }
}

static esp_err_t ui_prepare_stage(ui_stage_t stage)
{
    LV_UNUSED(stage);
    return ESP_OK;
}

static void ui_transition_start_fade_in(void)
{
    lv_anim_t anim;

    /* The title owns its 1200 ms reveal animation.  A second 700 ms outer
     * fade delayed its sonic logo and made restart feel unresponsive. */
    if (s_current_stage == UI_STAGE_TITLE) {
        ui_reset_transition_overlay();
        ui_start_title_sequence();
        return;
    }

    lv_obj_t *overlay = ui_ensure_transition_overlay();
    if (!overlay) {
        if (s_current_stage == UI_STAGE_STORY) {
            ui_story_stage_ready();
        }
        return;
    }

    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay);
    lv_anim_del(overlay, ui_transition_overlay_set_opa_cb);
    ui_transition_overlay_set_opa_cb(overlay, LV_OPA_COVER);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, overlay);
    lv_anim_set_exec_cb(&anim, ui_transition_overlay_set_opa_cb);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&anim, UI_MAINSCREEN_FADE_IN_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, ui_transition_fade_in_ready_cb);
    lv_anim_start(&anim);
}

static void ui_transition_fade_out_ready_cb(lv_anim_t *anim)
{
    LV_UNUSED(anim);

    if (s_transition_overlay) {
        ui_transition_overlay_set_opa_cb(s_transition_overlay, LV_OPA_COVER);
    }

    if (s_story_reset_pending) {
        story_coordinator_reset();
        s_story_reset_pending = false;
    }

    ui_cancel_stage_prepare_timer();
    s_stage_prepare_timer = lv_timer_create(ui_stage_prepare_timer_cb, 1, NULL);
    if (s_stage_prepare_timer) {
        lv_timer_set_repeat_count(s_stage_prepare_timer, 1);
    } else {
        ui_finish_stage_transition();
    }
}

static void ui_finish_stage_transition(void)
{
    lv_obj_t *previous_screen = lv_scr_act();
    lv_obj_t *next_screen = NULL;
    esp_err_t prep_err = ESP_OK;

    prep_err = ui_prepare_stage(s_pending_stage);
    if (prep_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to prepare UI stage %d: %s", (int)s_pending_stage, esp_err_to_name(prep_err));
    }

    next_screen = ui_create_stage_screen(s_pending_stage);
    if (!next_screen) {
        ui_reset_transition_overlay();
        return;
    }

    s_current_stage = s_pending_stage;
    lv_disp_load_scr(next_screen);

    if (previous_screen && previous_screen != next_screen) {
        lv_obj_del(previous_screen);
    }

    ui_transition_start_fade_in();
}

static void ui_stage_prepare_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    s_stage_prepare_timer = NULL;
    ui_finish_stage_transition();
}

static void ui_transition_to_stage(ui_stage_t stage)
{
    lv_anim_t anim;
    lv_obj_t *overlay = NULL;

    s_pending_stage = stage;
    overlay = ui_ensure_transition_overlay();
    if (!overlay) {
        ui_transition_fade_out_ready_cb(NULL);
        return;
    }

    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay);
    lv_anim_del(overlay, ui_transition_overlay_set_opa_cb);
    ui_transition_overlay_set_opa_cb(overlay, LV_OPA_TRANSP);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, overlay);
    lv_anim_set_exec_cb(&anim, ui_transition_overlay_set_opa_cb);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&anim, UI_TITLESCREEN_FADE_OUT_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, ui_transition_fade_out_ready_cb);
    lv_anim_start(&anim);
}

static void ui_title_transition_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    s_title_transition_timer = NULL;
    (void)story_audio_fade_out_cues(UI_TITLESCREEN_FADE_OUT_MS);
    ui_transition_to_stage(UI_STAGE_STORY);
}

static void ui_cancel_mode_hold(void)
{
    s_mode_hold_active = false;
    if (s_mode_hold_timer) {
        (void)esp_timer_stop(s_mode_hold_timer);
    }
}

static void ui_close_mode_overlay(void)
{
    if (s_mode_overlay) {
        lv_obj_del(s_mode_overlay);
        s_mode_overlay = NULL;
    }
    if (s_mode_hotspot) {
        lv_obj_move_foreground(s_mode_hotspot);
    }
}

static void ui_mode_overlay_background_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
        lv_event_get_target(event) == s_mode_overlay) {
        ui_close_mode_overlay();
    }
}

static void ui_mode_overlay_cancel_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        ui_close_mode_overlay();
    }
}

static void ui_mode_overlay_switch_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    story_app_mode_t next_mode = story_coordinator_get_mode() == STORY_APP_MODE_LOOP
                                     ? STORY_APP_MODE_FULL
                                     : STORY_APP_MODE_LOOP;
    ui_close_mode_overlay();
    story_coordinator_switch_mode(next_mode);
}

static lv_obj_t *ui_create_mode_button(lv_obj_t *parent,
                                       const char *text,
                                       lv_event_cb_t callback,
                                       bool secondary)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, LV_SIZE_CONTENT, 72);
    lv_obj_set_style_radius(button, 36, LV_PART_MAIN);
    lv_obj_set_style_pad_left(button, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_right(button, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_top(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(secondary ? 0x7A3E1D : 0xFAF190),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(secondary ? 0x5A2B16 : 0xECCA5A),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(secondary ? 0xFAF190 : 0x490909),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

static void ui_show_mode_overlay(void)
{
    if (s_mode_overlay) {
        return;
    }

    s_mode_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_mode_overlay);
    lv_obj_set_size(s_mode_overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(s_mode_overlay);
    lv_obj_add_flag(s_mode_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_mode_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_mode_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_mode_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_add_event_cb(s_mode_overlay,
                        ui_mode_overlay_background_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *panel = lv_obj_create(s_mode_overlay);
    lv_obj_set_size(panel, 620, 290);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(panel, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xFAF190), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);

    lv_obj_t *question_area = lv_obj_create(panel);
    lv_obj_remove_style_all(question_area);
    lv_obj_set_size(question_area, 560, 155);
    lv_obj_align(question_area, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_clear_flag(question_area, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(question_area);
    lv_obj_set_width(title, 540);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_label_set_text(title,
                      story_coordinator_get_mode() == STORY_APP_MODE_LOOP
                          ? "Open the full story?"
                          : "Return to story loop?");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFAF190), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_36, LV_PART_MAIN);
    lv_obj_center(title);

    lv_obj_t *switch_button = ui_create_mode_button(
        panel,
        story_coordinator_get_mode() == STORY_APP_MODE_LOOP
            ? "Open full story"
            : "Return to story loop",
        ui_mode_overlay_switch_event_cb,
        false);
    lv_obj_align(switch_button, LV_ALIGN_BOTTOM_LEFT, 30, -35);

    lv_obj_t *cancel_button = ui_create_mode_button(panel,
                                                    "Cancel",
                                                    ui_mode_overlay_cancel_event_cb,
                                                    true);
    lv_obj_align(cancel_button, LV_ALIGN_BOTTOM_RIGHT, -30, -35);
    lv_obj_move_foreground(s_mode_overlay);
}

static void ui_mode_hold_timer_cb(void *ctx)
{
    LV_UNUSED(ctx);

    if (!s_mode_hold_active) {
        return;
    }

    if (!lvgl_port_lock(UI_MODE_LOCK_TIMEOUT_MS)) {
        if (s_mode_hold_active) {
            (void)esp_timer_start_once(s_mode_hold_timer, UI_MODE_LOCK_RETRY_US);
        }
        return;
    }

    if (s_mode_hold_active) {
        int64_t refresh_started_us = 0;

        s_mode_hold_active = false;
        ESP_LOGI(TAG,
                 "mode overlay opened after %lld ms",
                 (long long)((esp_timer_get_time() - s_mode_hold_started_us) / 1000));
        ui_show_mode_overlay();
        refresh_started_us = esp_timer_get_time();
        lv_refr_now(lv_disp_get_default());
        ESP_LOGI(TAG,
                 "mode overlay refresh completed in %lld ms",
                 (long long)((esp_timer_get_time() - refresh_started_us) / 1000));
    }
    lvgl_port_unlock();
}

static void ui_mode_hotspot_event_cb(lv_event_t *event)
{
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        ui_cancel_mode_hold();
        if (!s_mode_hold_timer) {
            const esp_timer_create_args_t timer_args = {
                .callback = ui_mode_hold_timer_cb,
                .name = "mode_hold",
            };
            if (esp_timer_create(&timer_args, &s_mode_hold_timer) != ESP_OK) {
                ESP_LOGE(TAG, "unable to create mode hold timer");
                break;
            }
        }
        s_mode_hold_active = true;
        s_mode_hold_started_us = esp_timer_get_time();
        if (esp_timer_start_once(s_mode_hold_timer, UI_MODE_HOLD_MS * 1000U) != ESP_OK) {
            s_mode_hold_active = false;
            ESP_LOGE(TAG, "unable to start mode hold timer");
        }
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
    case LV_EVENT_DELETE:
        ui_cancel_mode_hold();
        break;
    default:
        break;
    }
}

static void ui_create_mode_hotspot(void)
{
    if (s_mode_hotspot) {
        return;
    }

    s_mode_hotspot = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_mode_hotspot);
    lv_obj_set_size(s_mode_hotspot, UI_MODE_HOTSPOT_SIZE, UI_MODE_HOTSPOT_SIZE);
    lv_obj_align(s_mode_hotspot, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_flag(s_mode_hotspot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_mode_hotspot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_mode_hotspot, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(s_mode_hotspot, ui_mode_hotspot_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_move_foreground(s_mode_hotspot);
}

static void ui_mode_hint_delete_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_mode_hint_overlay = NULL;
    s_mode_hint_checkbox = NULL;
}

static void ui_close_mode_hint(bool start_story)
{
    if (s_mode_hint_overlay) {
        lv_obj_del(s_mode_hint_overlay);
    }
    if (s_mode_hotspot) {
        lv_obj_move_foreground(s_mode_hotspot);
    }
    if (start_story && s_current_stage == UI_STAGE_STORY) {
        ui_story_start_if_needed();
    }
}

static void ui_mode_hint_ok_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_mode_hint_checkbox && lv_obj_has_state(s_mode_hint_checkbox, LV_STATE_CHECKED)) {
        esp_err_t err = settings_store_set_mode_hint_hidden(true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unable to save mode hint preference: %s", esp_err_to_name(err));
        }
    }
    ui_close_mode_hint(true);
}

static bool ui_show_mode_hint(void)
{
    lv_obj_t *story_screen = lv_scr_act();
    if (!story_screen || s_current_stage != UI_STAGE_STORY) {
        return false;
    }

    /* Keep the onboarding hint inside the active story screen so LVGL
     * snapshots include it. The persistent mode hotspot stays on top. */
    s_mode_hint_overlay = lv_obj_create(story_screen);
    lv_obj_remove_style_all(s_mode_hint_overlay);
    lv_obj_set_size(s_mode_hint_overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(s_mode_hint_overlay);
    lv_obj_add_flag(s_mode_hint_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_mode_hint_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_mode_hint_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_mode_hint_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_add_event_cb(s_mode_hint_overlay, ui_mode_hint_delete_event_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *highlight = lv_obj_create(s_mode_hint_overlay);
    lv_obj_set_size(highlight, UI_MODE_HOTSPOT_SIZE, UI_MODE_HOTSPOT_SIZE);
    lv_obj_align(highlight, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_clear_flag(highlight, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(highlight, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(highlight, lv_color_hex(0xECCA5A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(highlight, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_color(highlight, lv_color_hex(0xFAF190), LV_PART_MAIN);
    lv_obj_set_style_border_width(highlight, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(highlight, 0, LV_PART_MAIN);

    lv_obj_t *hold_label = lv_label_create(highlight);
    lv_label_set_text(hold_label, "Hold 2 sec");
    lv_obj_set_style_text_color(hold_label, lv_color_hex(0xFBF4D7), LV_PART_MAIN);
    lv_obj_set_style_text_font(hold_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(hold_label);

    lv_obj_t *panel = lv_obj_create(s_mode_hint_overlay);
    lv_obj_set_size(panel, 620, 370);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(panel, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xFAF190), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(panel, 0, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_width(title, 560);
    lv_label_set_text(title, "Welcome to Story Loop");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFAF190), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *message = lv_label_create(panel);
    lv_obj_set_width(message, 520);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_label_set_text(message,
                      "You are currently in Story Loop, where the story plays automatically.\n\n"
                      "To enter Full Story, press and hold the highlighted upper-right corner "
                      "for 2 seconds, then select Open full story.");
    lv_obj_set_style_text_color(message, lv_color_hex(0xFBF4D7), LV_PART_MAIN);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(message, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 88);

    s_mode_hint_checkbox = lv_checkbox_create(panel);
    lv_checkbox_set_text(s_mode_hint_checkbox, "Don't show it again");
    lv_obj_align(s_mode_hint_checkbox, LV_ALIGN_BOTTOM_MID, 0, -122);
    lv_obj_set_style_text_color(s_mode_hint_checkbox, lv_color_hex(0xE7D7A1), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_mode_hint_checkbox, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_mode_hint_checkbox, 16, LV_PART_MAIN);
    /* LVGL derives checkbox indicator size from the main font height plus
     * indicator padding; explicit width/height styles are ignored here. */
    lv_obj_set_style_pad_all(s_mode_hint_checkbox, 3, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_mode_hint_checkbox, 5, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_mode_hint_checkbox, 3, LV_PART_INDICATOR);
    lv_obj_set_style_border_opa(s_mode_hint_checkbox, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(s_mode_hint_checkbox, lv_color_hex(0xFAF190), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_mode_hint_checkbox, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_mode_hint_checkbox,
                              lv_color_hex(0xECCA5A),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(s_mode_hint_checkbox,
                            LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_mode_hint_checkbox,
                                lv_color_hex(0x490909),
                                LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(s_mode_hint_checkbox,
                               &lv_font_montserrat_18,
                               LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_img_src(s_mode_hint_checkbox,
                                LV_SYMBOL_OK,
                                LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_img_recolor(s_mode_hint_checkbox,
                                    lv_color_hex(0x490909),
                                    LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_img_recolor_opa(s_mode_hint_checkbox,
                                        LV_OPA_COVER,
                                        LV_PART_INDICATOR | LV_STATE_CHECKED);

    lv_obj_t *ok_button = ui_create_mode_button(panel,
                                                "Got it",
                                                ui_mode_hint_ok_event_cb,
                                                false);
    lv_obj_align(ok_button, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_move_foreground(s_mode_hint_overlay);
    if (s_mode_hotspot) {
        lv_obj_move_foreground(s_mode_hotspot);
    }
    return true;
}

void ui_init(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    bool mode_hint_hidden = false;

    if (disp) {
        lv_disp_set_theme(disp, NULL);
    }

    s_story_started = false;
    s_current_stage = UI_STAGE_TITLE;
    s_pending_stage = UI_STAGE_NONE;
    s_story_reset_pending = false;
    ui_cancel_title_transition_timer();
    ui_cancel_stage_prepare_timer();
    ui_reset_transition_overlay();

    /* Keep the boot/loading screen visible while the large story media is
     * prepared.  ui_start_title_sequence() repeats only cheap cache lookups. */
    ui_dual_gif_screen_preload_initial_media();
    ui_TitleScreen_screen_init();
    lv_disp_load_scr(ui_TitleScreen);
    ui_create_mode_hotspot();

    if (!s_mode_hint_preference_loaded) {
        esp_err_t err = settings_store_get_mode_hint_hidden(&mode_hint_hidden);
        s_mode_hint_preference_loaded = true;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unable to read mode hint preference: %s", esp_err_to_name(err));
            mode_hint_hidden = false;
        }
        s_mode_hint_pending = !mode_hint_hidden;
    }

    ui_start_title_sequence();
}

void ui_destroy(void)
{
    ui_cancel_title_transition_timer();
    ui_cancel_stage_prepare_timer();
    ui_cancel_mode_hold();
    if (s_mode_hold_timer) {
        (void)esp_timer_delete(s_mode_hold_timer);
        s_mode_hold_timer = NULL;
    }
    ui_close_mode_hint(false);
    ui_close_mode_overlay();
    if (s_mode_hotspot) {
        lv_obj_del(s_mode_hotspot);
        s_mode_hotspot = NULL;
    }
    ui_reset_transition_overlay();
    ui_TitleScreen_screen_destroy();
    ui_dual_gif_screen_destroy();
    s_story_started = false;
    s_current_stage = UI_STAGE_NONE;
    s_pending_stage = UI_STAGE_NONE;
    s_story_reset_pending = false;
}

void ui_request_restart_to_title(void)
{
    ui_cancel_title_transition_timer();
    if (s_current_stage == UI_STAGE_STORY || s_pending_stage == UI_STAGE_STORY) {
        s_story_reset_pending = true;
    }
    s_story_started = false;
    ui_transition_to_stage(UI_STAGE_TITLE);
}

void ui_story_bird_talk_anim_start(void)
{
    if (audio_session_is_muted()) {
        ui_dual_gif_screen_request_bird_state(UI_DUAL_GIF_BIRD_STATE_IDLE);
        return;
    }

    /* Playback starts outside the UI thread, so only request TALK_LOOP here.
     * The lightweight bird timer applies it on the next frame boundary. */
    ui_dual_gif_screen_request_bird_state(UI_DUAL_GIF_BIRD_STATE_TALK_LOOP);
}

void ui_story_bird_talk_anim_stop(void)
{
    ui_dual_gif_screen_request_bird_state(UI_DUAL_GIF_BIRD_STATE_IDLE);
}

void ui_notify_tts_finished(void)
{
    /* Legacy compatibility no-op. Story flow advances from audio_session
     * completion events on the UI thread. */
}

void ui_notify_audio_playback_finished(void)
{
    /* Legacy compatibility no-op. */
}
