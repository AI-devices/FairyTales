// This file was adapted from SquareLine-generated ui_Screen3.c

#include "ui_TitleScreen.h"

#include <stdbool.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "story_audio.h"
#include "ui_img_manager.h"

#define UI_TITLESCREEN_INTRO_MS 1200U
#define UI_TITLESCREEN_SONIC_LOGO_DELAY_MS 120U
#define UI_TITLESCREEN_STAR_BLINK_REPEAT_COUNT 5U

#define TITLE_SCREEN_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

LV_FONT_DECLARE(ui_font_Font0);
LV_FONT_DECLARE(ui_font_Font00);

typedef enum {
    TITLE_SCREEN_ASSET_STAR_BIG = 0,
    TITLE_SCREEN_ASSET_STAR_MID,
    TITLE_SCREEN_ASSET_STAR_MINI,
    TITLE_SCREEN_ASSET_SPARK_BIG,
    TITLE_SCREEN_ASSET_SPARK_MID,
    TITLE_SCREEN_ASSET_SPARK_MINI,
    TITLE_SCREEN_ASSET_COUNT
} title_screen_asset_id_t;

typedef struct {
    const char *spiffs_path;
    uint32_t raw_size;
    uint8_t *blob;
    lv_img_dsc_t img_dsc;
} title_screen_asset_t;

typedef struct {
    lv_obj_t **obj_ref;
    lv_coord_t final_y;
} title_screen_cloud_panel_anim_t;

typedef struct {
    lv_obj_t **obj_ref;
    uint16_t start_delay_ms;
    uint16_t half_cycle_ms;
    uint16_t repeat_delay_ms;
    lv_opa_t min_opa;
} title_screen_star_shadow_anim_t;

lv_obj_t *ui_TitleScreen = NULL;

static lv_obj_t *s_label_top = NULL;
static lv_obj_t *s_label_bottom = NULL;
static lv_obj_t *s_panel_left_1 = NULL;
static lv_obj_t *s_panel_right_1 = NULL;
static lv_obj_t *s_panel_left_2 = NULL;
static lv_obj_t *s_panel_left_3 = NULL;
static lv_obj_t *s_panel_left_4 = NULL;
static lv_obj_t *s_panel_right_2 = NULL;
static lv_obj_t *s_panel_right_3 = NULL;
static lv_obj_t *s_panel_right_4 = NULL;
static lv_obj_t *s_star_shadow_1 = NULL;
static lv_obj_t *s_star_1 = NULL;
static lv_obj_t *s_star_shadow_2 = NULL;
static lv_obj_t *s_star_2 = NULL;
static lv_obj_t *s_star_shadow_3 = NULL;
static lv_obj_t *s_star_3 = NULL;
static lv_obj_t *s_spark_1 = NULL;
static lv_obj_t *s_spark_2 = NULL;
static lv_obj_t *s_spark_3 = NULL;
static lv_obj_t *s_spark_4 = NULL;
static lv_obj_t *s_star_shadow_4 = NULL;
static lv_obj_t *s_star_4 = NULL;
static lv_obj_t *s_spark_5 = NULL;
static lv_obj_t *s_star_shadow_5 = NULL;
static lv_obj_t *s_star_5 = NULL;
static lv_obj_t *s_star_shadow_6 = NULL;
static lv_obj_t *s_star_6 = NULL;
static lv_obj_t *s_spark_6 = NULL;
static lv_obj_t *s_star_shadow_7 = NULL;
static lv_obj_t *s_star_7 = NULL;
static lv_obj_t *s_spark_7 = NULL;
static lv_obj_t *s_star_shadow_8 = NULL;
static lv_obj_t *s_star_8 = NULL;
static lv_obj_t *s_intro_overlay = NULL;
static lv_timer_t *s_sonic_logo_timer = NULL;

static title_screen_asset_t s_title_assets[TITLE_SCREEN_ASSET_COUNT] = {
    [TITLE_SCREEN_ASSET_STAR_BIG] = {
        .spiffs_path = "S:/assets/screen_stars/star_big.bin",
        .raw_size = 19204U,
    },
    [TITLE_SCREEN_ASSET_STAR_MID] = {
        .spiffs_path = "S:/assets/screen_stars/star_mid.bin",
        .raw_size = 7504U,
    },
    [TITLE_SCREEN_ASSET_STAR_MINI] = {
        .spiffs_path = "S:/assets/screen_stars/star_mini.bin",
        .raw_size = 2704U,
    },
    [TITLE_SCREEN_ASSET_SPARK_BIG] = {
        .spiffs_path = "S:/assets/screen_stars/spark_big.bin",
        .raw_size = 7504U,
    },
    [TITLE_SCREEN_ASSET_SPARK_MID] = {
        .spiffs_path = "S:/assets/screen_stars/spark_mid.bin",
        .raw_size = 2704U,
    },
    [TITLE_SCREEN_ASSET_SPARK_MINI] = {
        .spiffs_path = "S:/assets/screen_stars/spark_mini.bin",
        .raw_size = 1204U,
    },
};

static const title_screen_cloud_panel_anim_t s_cloud_group_left[] = {
    { &s_panel_left_1, 209 },
    { &s_panel_left_2, 248 },
    { &s_panel_left_3, 298 },
    { &s_panel_left_4, 330 },
};

static const title_screen_cloud_panel_anim_t s_cloud_group_right[] = {
    { &s_panel_right_1, 271 },
    { &s_panel_right_2, 152 },
    { &s_panel_right_3, 268 },
    { &s_panel_right_4, 169 },
};

static const title_screen_star_shadow_anim_t s_star_shadow_anims[] = {
    { &s_star_shadow_1, 0, 820, 120, LV_OPA_TRANSP },
    { &s_star_shadow_2, 160, 700, 180, LV_OPA_TRANSP },
    { &s_star_shadow_3, 320, 930, 60, LV_OPA_TRANSP },
    { &s_star_shadow_4, 540, 640, 220, LV_OPA_TRANSP },
    { &s_star_shadow_5, 760, 780, 110, LV_OPA_TRANSP },
    { &s_star_shadow_6, 980, 860, 170, LV_OPA_TRANSP },
    { &s_star_shadow_7, 1180, 690, 230, LV_OPA_TRANSP },
    { &s_star_shadow_8, 1400, 970, 90, LV_OPA_TRANSP },
};

static void title_screen_anim_set_opa_cb(void *var, int32_t value);
static void title_screen_cancel_anims(void);
static void title_screen_release_assets(void);
static void title_screen_reset_object_refs(void);

static bool title_screen_load_asset(title_screen_asset_t *asset)
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

static const lv_img_dsc_t *title_screen_get_asset(title_screen_asset_id_t asset_id)
{
    title_screen_asset_t *asset = NULL;

    if ((unsigned)asset_id >= TITLE_SCREEN_ASSET_COUNT) {
        return NULL;
    }

    asset = &s_title_assets[asset_id];
    if (!title_screen_load_asset(asset)) {
        return NULL;
    }

    return &asset->img_dsc;
}

static void title_screen_release_assets(void)
{
    size_t i = 0;

    for (i = 0; i < TITLE_SCREEN_ASSET_COUNT; ++i) {
        /* LVGL identifies a variable image by the lv_img_dsc_t address and
         * keeps that source in its image cache.  s_title_assets is static, so
         * the descriptor address is reused on every title-screen recreation.
         * Invalidate it before freeing the backing blob; otherwise LVGL can
         * reuse a cached decoder whose data still points to the old PSRAM
         * allocation, which has usually been overwritten after a few story
         * runs and is then rendered as coloured star/spark artifacts. */
        lv_img_cache_invalidate_src(&s_title_assets[i].img_dsc);

        if (s_title_assets[i].blob) {
            heap_caps_free(s_title_assets[i].blob);
            s_title_assets[i].blob = NULL;
        }

        memset(&s_title_assets[i].img_dsc, 0, sizeof(s_title_assets[i].img_dsc));
    }
}

static void title_screen_reset_object_refs(void)
{
    ui_TitleScreen = NULL;
    s_label_top = NULL;
    s_label_bottom = NULL;
    s_panel_left_1 = NULL;
    s_panel_right_1 = NULL;
    s_panel_left_2 = NULL;
    s_panel_left_3 = NULL;
    s_panel_left_4 = NULL;
    s_panel_right_2 = NULL;
    s_panel_right_3 = NULL;
    s_panel_right_4 = NULL;
    s_star_shadow_1 = NULL;
    s_star_1 = NULL;
    s_star_shadow_2 = NULL;
    s_star_2 = NULL;
    s_star_shadow_3 = NULL;
    s_star_3 = NULL;
    s_spark_1 = NULL;
    s_spark_2 = NULL;
    s_spark_3 = NULL;
    s_spark_4 = NULL;
    s_star_shadow_4 = NULL;
    s_star_4 = NULL;
    s_spark_5 = NULL;
    s_star_shadow_5 = NULL;
    s_star_5 = NULL;
    s_star_shadow_6 = NULL;
    s_star_6 = NULL;
    s_spark_6 = NULL;
    s_star_shadow_7 = NULL;
    s_star_7 = NULL;
    s_spark_7 = NULL;
    s_star_shadow_8 = NULL;
    s_star_8 = NULL;
    s_intro_overlay = NULL;
}

static void title_screen_anim_set_opa_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;

    if (!obj) {
        return;
    }

    lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
}

static void title_screen_anim_set_y_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;

    if (!obj) {
        return;
    }

    lv_obj_set_y(obj, (lv_coord_t)value);
}

static void title_screen_root_delete_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);

    title_screen_cancel_anims();
    title_screen_release_assets();
    title_screen_reset_object_refs();
}

static void title_screen_init_cloud_panel(lv_obj_t **obj_ref, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *obj = lv_obj_create(ui_TitleScreen);

    *obj_ref = obj;

    lv_obj_set_width(obj, 50);
    lv_obj_set_height(obj, 50);
    lv_obj_set_x(obj, x);
    lv_obj_set_y(obj, y);
    lv_obj_set_align(obj, LV_ALIGN_CENTER);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(obj, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void title_screen_init_star_shadow(lv_obj_t **obj_ref,
                                          lv_coord_t width,
                                          lv_coord_t height,
                                          lv_coord_t x,
                                          lv_coord_t y,
                                          lv_coord_t shadow_width,
                                          lv_coord_t shadow_spread)
{
    lv_obj_t *obj = lv_obj_create(ui_TitleScreen);

    *obj_ref = obj;

    lv_obj_set_width(obj, width);
    lv_obj_set_height(obj, height);
    lv_obj_set_x(obj, x);
    lv_obj_set_y(obj, y);
    lv_obj_set_align(obj, LV_ALIGN_CENTER);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, shadow_width, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(obj, shadow_spread, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void title_screen_init_img(lv_obj_t **obj_ref,
                                  title_screen_asset_id_t asset_id,
                                  lv_coord_t x,
                                  lv_coord_t y,
                                  int16_t angle,
                                  bool set_angle)
{
    lv_obj_t *obj = lv_img_create(ui_TitleScreen);
    const lv_img_dsc_t *img_dsc = title_screen_get_asset(asset_id);

    *obj_ref = obj;

    if (img_dsc) {
        lv_img_set_src(obj, img_dsc);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_width(obj, LV_SIZE_CONTENT);
    lv_obj_set_height(obj, LV_SIZE_CONTENT);
    lv_obj_set_x(obj, x);
    lv_obj_set_y(obj, y);
    lv_obj_set_align(obj, LV_ALIGN_CENTER);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_ADV_HITTEST | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    if (set_angle) {
        lv_img_set_angle(obj, angle);
    }
}

static void title_screen_cancel_cloud_group_anims(const title_screen_cloud_panel_anim_t *group, size_t count)
{
    size_t i = 0;

    for (i = 0; i < count; ++i) {
        if (*group[i].obj_ref) {
            lv_anim_del(*group[i].obj_ref, title_screen_anim_set_y_cb);
        }
    }
}

static void title_screen_cancel_star_shadow_anims(void)
{
    size_t i = 0;

    for (i = 0; i < TITLE_SCREEN_ARRAY_SIZE(s_star_shadow_anims); ++i) {
        lv_obj_t *obj = *s_star_shadow_anims[i].obj_ref;

        if (!obj) {
            continue;
        }

        lv_anim_del(obj, title_screen_anim_set_opa_cb);
        title_screen_anim_set_opa_cb(obj, LV_OPA_COVER);
    }
}

static void title_screen_cancel_anims(void)
{
    if (s_sonic_logo_timer) {
        lv_timer_del(s_sonic_logo_timer);
        s_sonic_logo_timer = NULL;
    }
    if (s_intro_overlay) {
        lv_anim_del(s_intro_overlay, title_screen_anim_set_opa_cb);
    }

    title_screen_cancel_cloud_group_anims(s_cloud_group_left, TITLE_SCREEN_ARRAY_SIZE(s_cloud_group_left));
    title_screen_cancel_cloud_group_anims(s_cloud_group_right, TITLE_SCREEN_ARRAY_SIZE(s_cloud_group_right));
    title_screen_cancel_star_shadow_anims();
}

static void title_screen_sonic_logo_timer_cb(lv_timer_t *timer)
{
    if (s_sonic_logo_timer == timer) {
        s_sonic_logo_timer = NULL;
    }
    (void)story_audio_play_cue(STORY_AUDIO_CUE_TITLE_SONIC_LOGO);
}

static void title_screen_set_cloud_group_y(const title_screen_cloud_panel_anim_t *group,
                                           size_t count,
                                           lv_coord_t offset_y)
{
    size_t i = 0;

    for (i = 0; i < count; ++i) {
        if (*group[i].obj_ref) {
            lv_obj_set_y(*group[i].obj_ref, group[i].final_y + offset_y);
        }
    }
}

static void title_screen_start_cloud_group_anim(const title_screen_cloud_panel_anim_t *group,
                                                size_t count,
                                                lv_coord_t start_offset_y,
                                                uint32_t duration_ms)
{
    size_t i = 0;

    for (i = 0; i < count; ++i) {
        lv_obj_t *obj = *group[i].obj_ref;
        lv_anim_t anim;

        if (!obj) {
            continue;
        }

        lv_obj_set_y(obj, group[i].final_y + start_offset_y);

        lv_anim_init(&anim);
        lv_anim_set_var(&anim, obj);
        lv_anim_set_exec_cb(&anim, title_screen_anim_set_y_cb);
        lv_anim_set_values(&anim, group[i].final_y + start_offset_y, group[i].final_y);
        lv_anim_set_time(&anim, duration_ms);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
    }
}

static void title_screen_start_star_shadow_blink(const title_screen_star_shadow_anim_t *config)
{
    lv_obj_t *obj = NULL;
    lv_anim_t anim;

    if (!config) {
        return;
    }

    obj = *config->obj_ref;
    if (!obj) {
        return;
    }

    title_screen_anim_set_opa_cb(obj, LV_OPA_COVER);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, title_screen_anim_set_opa_cb);
    lv_anim_set_values(&anim, LV_OPA_COVER, config->min_opa);
    lv_anim_set_time(&anim, config->half_cycle_ms);
    lv_anim_set_playback_time(&anim, config->half_cycle_ms);
    lv_anim_set_delay(&anim, config->start_delay_ms);
    lv_anim_set_repeat_delay(&anim, config->repeat_delay_ms);
    lv_anim_set_repeat_count(&anim, UI_TITLESCREEN_STAR_BLINK_REPEAT_COUNT);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
}

static void title_screen_start_star_shadow_anims(void)
{
    size_t i = 0;

    for (i = 0; i < TITLE_SCREEN_ARRAY_SIZE(s_star_shadow_anims); ++i) {
        title_screen_start_star_shadow_blink(&s_star_shadow_anims[i]);
    }
}

static void title_screen_intro_ready_cb(lv_anim_t *anim)
{
    LV_UNUSED(anim);

    if (s_intro_overlay) {
        title_screen_anim_set_opa_cb(s_intro_overlay, LV_OPA_TRANSP);
        lv_obj_add_flag(s_intro_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    title_screen_start_star_shadow_anims();
}

void ui_TitleScreen_start_intro_animation(void)
{
    lv_anim_t anim;
    const lv_coord_t start_offset_y = (lv_coord_t)lv_disp_get_ver_res(NULL);

    if (!ui_TitleScreen || !s_intro_overlay) {
        return;
    }

    title_screen_cancel_anims();

    /* Both reference branches start the sonic logo with the intro.  A short
     * timer keeps it off the fully black first frame while avoiding the
     * one-second delay caused by waiting for the whole reveal animation. */
    s_sonic_logo_timer = lv_timer_create(title_screen_sonic_logo_timer_cb,
                                         UI_TITLESCREEN_SONIC_LOGO_DELAY_MS,
                                         NULL);
    if (s_sonic_logo_timer) {
        lv_timer_set_repeat_count(s_sonic_logo_timer, 1);
    } else {
        (void)story_audio_play_cue(STORY_AUDIO_CUE_TITLE_SONIC_LOGO);
    }

    lv_obj_clear_flag(s_intro_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_intro_overlay);
    title_screen_anim_set_opa_cb(s_intro_overlay, LV_OPA_COVER);

    title_screen_start_cloud_group_anim(s_cloud_group_left,
                                        TITLE_SCREEN_ARRAY_SIZE(s_cloud_group_left),
                                        start_offset_y,
                                        UI_TITLESCREEN_INTRO_MS);
    title_screen_start_cloud_group_anim(s_cloud_group_right,
                                        TITLE_SCREEN_ARRAY_SIZE(s_cloud_group_right),
                                        start_offset_y,
                                        UI_TITLESCREEN_INTRO_MS);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_intro_overlay);
    lv_anim_set_exec_cb(&anim, title_screen_anim_set_opa_cb);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&anim, UI_TITLESCREEN_INTRO_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_ready_cb(&anim, title_screen_intro_ready_cb);
    lv_anim_start(&anim);
}

void ui_TitleScreen_screen_init(void)
{
    title_screen_cancel_anims();

    if (ui_TitleScreen) {
        return;
    }

    title_screen_reset_object_refs();

    ui_TitleScreen = lv_obj_create(NULL);
    lv_obj_add_event_cb(ui_TitleScreen, title_screen_root_delete_event_cb, LV_EVENT_DELETE, NULL);
    lv_obj_clear_flag(ui_TitleScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ui_TitleScreen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_TitleScreen,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ui_TitleScreen, lv_color_hex(0x171717), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_TitleScreen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui_TitleScreen, lv_color_hex(0xD8D8D8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui_TitleScreen, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(ui_TitleScreen, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_TitleScreen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_label_top = lv_label_create(ui_TitleScreen);
    lv_obj_set_width(s_label_top, LV_SIZE_CONTENT);
    lv_obj_set_height(s_label_top, LV_SIZE_CONTENT);
    lv_obj_set_x(s_label_top, -6);
    lv_obj_set_y(s_label_top, -78);
    lv_obj_set_align(s_label_top, LV_ALIGN_CENTER);
    lv_label_set_text(s_label_top, "Magic Crow");
    lv_obj_set_style_text_color(s_label_top, lv_color_hex(0xECCA5A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(s_label_top, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_label_top, &ui_font_Font00, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_label_bottom = lv_label_create(ui_TitleScreen);
    lv_obj_set_width(s_label_bottom, LV_SIZE_CONTENT);
    lv_obj_set_height(s_label_bottom, LV_SIZE_CONTENT);
    lv_obj_set_x(s_label_bottom, -5);
    lv_obj_set_y(s_label_bottom, 89);
    lv_obj_set_align(s_label_bottom, LV_ALIGN_CENTER);
    lv_label_set_text(s_label_bottom, "Fairy Tale");
    lv_obj_set_style_text_color(s_label_bottom, lv_color_hex(0xECCA5A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(s_label_bottom, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_label_bottom, &ui_font_Font0, LV_PART_MAIN | LV_STATE_DEFAULT);

    title_screen_init_cloud_panel(&s_panel_left_1, -369, 209);
    title_screen_init_cloud_panel(&s_panel_right_1, 297, 271);
    title_screen_init_cloud_panel(&s_panel_left_2, -278, 248);
    title_screen_init_cloud_panel(&s_panel_left_3, -444, 298);
    title_screen_init_cloud_panel(&s_panel_left_4, -163, 330);
    title_screen_init_cloud_panel(&s_panel_right_2, 485, 152);
    title_screen_init_cloud_panel(&s_panel_right_3, 448, 268);
    title_screen_init_cloud_panel(&s_panel_right_4, 364, 169);

    title_screen_init_star_shadow(&s_star_shadow_1, 20, 20, -432, -229, 50, 15);
    title_screen_init_img(&s_star_1, TITLE_SCREEN_ASSET_STAR_BIG, -433, -233, -130, true);

    title_screen_init_star_shadow(&s_star_shadow_2, 10, 10, -353, -121, 40, 15);
    title_screen_init_img(&s_star_2, TITLE_SCREEN_ASSET_STAR_MID, -352, -124, 100, true);

    title_screen_init_star_shadow(&s_star_shadow_3, 4, 5, -211, -244, 40, 12);
    title_screen_init_img(&s_star_3, TITLE_SCREEN_ASSET_STAR_MINI, -211, -245, 9, true);

    title_screen_init_img(&s_spark_1, TITLE_SCREEN_ASSET_SPARK_BIG, -300, -208, -90, true);
    title_screen_init_img(&s_spark_2, TITLE_SCREEN_ASSET_SPARK_MID, -433, -129, -90, true);
    title_screen_init_img(&s_spark_3, TITLE_SCREEN_ASSET_SPARK_MINI, -372, -35, 0, false);
    title_screen_init_img(&s_spark_4, TITLE_SCREEN_ASSET_SPARK_MID, -337, -265, 90, true);

    title_screen_init_star_shadow(&s_star_shadow_4, 5, 5, -465, 0, 40, 12);
    title_screen_init_img(&s_star_4, TITLE_SCREEN_ASSET_STAR_MINI, -465, -2, 9, true);

    title_screen_init_img(&s_spark_5, TITLE_SCREEN_ASSET_SPARK_BIG, 359, -222, 120, true);

    title_screen_init_star_shadow(&s_star_shadow_5, 10, 10, 468, -58, 40, 15);
    title_screen_init_img(&s_star_5, TITLE_SCREEN_ASSET_STAR_MID, 468, -61, 100, true);

    title_screen_init_star_shadow(&s_star_shadow_6, 4, 5, 449, -265, 40, 12);
    title_screen_init_img(&s_star_6, TITLE_SCREEN_ASSET_STAR_MINI, 449, -266, 9, true);

    title_screen_init_img(&s_spark_6, TITLE_SCREEN_ASSET_SPARK_MID, 457, -180, 90, true);

    title_screen_init_star_shadow(&s_star_shadow_7, 4, 5, 400, -138, 40, 12);
    title_screen_init_img(&s_star_7, TITLE_SCREEN_ASSET_STAR_MINI, 400, -138, 9, true);

    title_screen_init_img(&s_spark_7, TITLE_SCREEN_ASSET_SPARK_MINI, 252, -264, 0, false);

    title_screen_init_star_shadow(&s_star_shadow_8, 4, 5, 274, -155, 40, 12);
    title_screen_init_img(&s_star_8, TITLE_SCREEN_ASSET_STAR_MINI, 274, -155, 9, true);

    s_intro_overlay = lv_obj_create(ui_TitleScreen);
    lv_obj_remove_style_all(s_intro_overlay);
    lv_obj_set_size(s_intro_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_align(s_intro_overlay, LV_ALIGN_CENTER);
    lv_obj_clear_flag(s_intro_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_intro_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(s_intro_overlay, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_intro_overlay, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_opa(s_intro_overlay, LV_OPA_COVER, 0);
    lv_obj_move_foreground(s_intro_overlay);

    title_screen_set_cloud_group_y(s_cloud_group_left, TITLE_SCREEN_ARRAY_SIZE(s_cloud_group_left), 0);
    title_screen_set_cloud_group_y(s_cloud_group_right, TITLE_SCREEN_ARRAY_SIZE(s_cloud_group_right), 0);
}

void ui_TitleScreen_screen_destroy(void)
{
    title_screen_cancel_anims();

    if (ui_TitleScreen) {
        lv_obj_del(ui_TitleScreen);
        return;
    }

    title_screen_release_assets();
    title_screen_reset_object_refs();
}
