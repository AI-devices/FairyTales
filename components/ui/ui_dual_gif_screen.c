#include "ui_dual_gif_screen.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "animation_decoder.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "bsp_board_extra.h"
#include "audio_session.h"
#include "media_cache.h"
#include "media_loader_service.h"
#include "settings_store.h"
#include "story_audio.h"
#include "story_media_manifest.h"
#include "ui.h"
#include "ui_dispatch.h"
#include "ui_img_manager.h"
#include "user_name_store.h"

#define UI_DUAL_GIF_OPEN_PATH STORY_MEDIA_URI_BOOK_OPEN
#define UI_DUAL_GIF_SWIPE_PATH STORY_MEDIA_URI_BOOK_SWIPE
#define UI_DUAL_GIF_CLOSE_PATH STORY_MEDIA_URI_BOOK_CLOSE
#define UI_DUAL_GIF_BIRD_START_PATH STORY_MEDIA_URI_BIRD_PRE_TALK
#define UI_DUAL_GIF_BIRD_LOOP_PATH STORY_MEDIA_URI_BIRD_TALK_LOOP
#define UI_DUAL_GIF_BIRD_IDLE_TEST_PATH STORY_MEDIA_URI_BIRD_IDLE
#define UI_DUAL_GIF_BIRD_INITIAL_STATE   UI_DUAL_GIF_BIRD_STATE_IDLE
#define UI_DUAL_GIF_TASK_CORE            1
#define UI_DUAL_GIF_TASK_PRIORITY        3
#define UI_DUAL_GIF_BOOK_TASK_PRIORITY   2
#define UI_DUAL_GIF_BIRD_TASK_PRIORITY   4
#define UI_DUAL_GIF_TASK_STACK           6144
#define UI_DUAL_GIF_TASK_STOP_WAIT_MS    1000U
#define UI_DUAL_GIF_PRELOAD_STOP_WAIT_MS 3000U
#define UI_DUAL_GIF_CONTROL_TASK_STACK   8192U
#define UI_DUAL_GIF_CONTROL_TASK_PRIORITY 2
#define UI_DUAL_GIF_CONTROL_TASK_CORE    0
#define UI_DUAL_GIF_STOP_WAIT_SLICE_MS   50U
#define UI_DUAL_GIF_PROFILE_LOG_EVERY    60U
#define UI_DUAL_GIF_CFA_PRELOAD_WAIT_MS  30000U
#define UI_DUAL_GIF_CFA_PRELOAD_POLL_MS  20U
#define UI_DUAL_GIF_CFA_PRELOAD_RETRY_MS 250U
#define UI_DUAL_GIF_BOOK_PRELOAD_AUDIO_STABLE_DELAY_MS 2000U
#define UI_DUAL_GIF_BOOK_OFFSET_X         0
#define UI_DUAL_GIF_BOOK_OFFSET_Y        30
#define UI_DUAL_GIF_BIRD_OFFSET_X       -112
#define UI_DUAL_GIF_BIRD_OFFSET_Y        22
#define UI_DUAL_GIF_BIRD_ENTRY_START_OFFSET_Y   7
#define UI_DUAL_GIF_BIRD_ENTRY_START_SHIFT_X  -200
#define UI_DUAL_GIF_BIRD_ENTRY_START_SHIFT_Y   (UI_DUAL_GIF_BIRD_ENTRY_START_OFFSET_Y - UI_DUAL_GIF_BIRD_OFFSET_Y)

#define UI_DUAL_GIF_BIRD_ENTRY_DELAY_MS       300U
#define UI_DUAL_GIF_BIRD_ENTRY_DURATION_MS    2000U

#define UI_DUAL_GIF_BOOK_OVERLAY_OFFSET_X    -214
#define UI_DUAL_GIF_BOOK_OVERLAY_OFFSET_Y      39
#define UI_DUAL_GIF_BOOK_OVERLAY_WIDTH        364
#define UI_DUAL_GIF_BOOK_OVERLAY_HEIGHT       246

#define UI_DUAL_GIF_OPEN_OVERLAY_SHOW_DELAY_MS   2500U
#define UI_DUAL_GIF_SWIPE_OVERLAY_SHOW_DELAY_MS  3100U
#define UI_DUAL_GIF_SWIPE_OVERLAY_HIDE_DELAY_MS  1200U

#define UI_DUAL_GIF_SWIPE_NEXT_IMAGE_FADE_IN_DELAY_MS  180U
#define UI_DUAL_GIF_CORNERS_FADE_IN_MS                 360U
#define UI_DUAL_GIF_CORNERS_FADE_IN_DELAY_MS           500U
#define UI_DUAL_GIF_CORNERS_FADE_OUT_DELAY_MS         1000U
#define UI_DUAL_GIF_CORNERS_FADE_OUT_MS                300U
#define UI_DUAL_GIF_OVERLAY_IMAGE_FADE_IN_MS           600U
#define UI_DUAL_GIF_OVERLAY_IMAGE_FADE_OUT_MS          100U
#define UI_DUAL_GIF_CORNER1_OFFSET_X         -154
#define UI_DUAL_GIF_CORNER1_OFFSET_Y          -98
#define UI_DUAL_GIF_CORNER2_OFFSET_X          156
#define UI_DUAL_GIF_CORNER2_OFFSET_Y          -95
#define UI_DUAL_GIF_CORNER2_ANGLE             900
#define UI_DUAL_GIF_CORNER3_OFFSET_X          154
#define UI_DUAL_GIF_CORNER3_OFFSET_Y           97
#define UI_DUAL_GIF_CORNER3_ANGLE            1800
#define UI_DUAL_GIF_CORNER4_OFFSET_X         -156
#define UI_DUAL_GIF_CORNER4_OFFSET_Y           95
#define UI_DUAL_GIF_CORNER4_ANGLE            -900
#define UI_DUAL_GIF_BOOK_STAGE_HOLD_DELAY_MS  3000U
#define UI_DUAL_GIF_POST_CLOSE_UI_FADE_IN_MS     0U

#define UI_DUAL_GIF_BIRD_RETURN_DELAY_MS      1000U
#define UI_DUAL_GIF_BIRD_RETURN_DURATION_MS  2000U

#define UI_DUAL_GIF_STORY_TEXT_OFFSET_X        215
#define UI_DUAL_GIF_STORY_TEXT_OFFSET_Y         30
#define UI_DUAL_GIF_STORY_TEXT_WIDTH           403
#define UI_DUAL_GIF_STORY_TEXT_HEIGHT          349
#define UI_DUAL_GIF_STORY_TEXT_PAD_X            12
#define UI_DUAL_GIF_STORY_TEXT_PAD_Y            12
#define UI_DUAL_GIF_STORY_TEXT_SCROLL_ANIM_MS  420U
#define UI_DUAL_GIF_STORY_TEXT_COLOR        0x490909
#define UI_DUAL_GIF_STORY_TEXT_FADE_IN_MS      600U
#define UI_DUAL_GIF_STORY_TEXT_FADE_OUT_MS     220U
#define UI_DUAL_GIF_STORY_TEXT_FADE_IN_DELAY_MS   800U
#define UI_DUAL_GIF_STORY_TEXT_FIRST_OPEN_EXTRA_DELAY_MS 400U
#define UI_DUAL_GIF_STORY_TEXT_FADE_OUT_DELAY_MS  0U
#define UI_DUAL_GIF_STORY_TEXT_SWIPE_FADE_IN_DELAY_MS   4500U
#define UI_DUAL_GIF_STORY_TEXT_SWIPE_FADE_OUT_DELAY_MS     500U
#define UI_DUAL_GIF_STORY_TEXT_AUTO_SCROLL_STEP_PX         2
#define UI_DUAL_GIF_STORY_TEXT_AUTO_SCROLL_PERIOD_MS     60U
#define UI_DUAL_GIF_STORY_TEXT_AUTO_SCROLL_NODE_A_PERIOD_MS 80U
#define UI_DUAL_GIF_STORY_CHOICE_OFFSET_X      211
#define UI_DUAL_GIF_STORY_CHOICE_OFFSET_Y       38
#define UI_DUAL_GIF_STORY_CHOICE_WIDTH         396
#define UI_DUAL_GIF_STORY_CHOICE_HEIGHT        369
#define UI_DUAL_GIF_STORY_CHOICE_OPTIONS_WIDTH 392
#define UI_DUAL_GIF_STORY_CHOICE_OPTIONS_HEIGHT 256
#define UI_DUAL_GIF_STORY_CHOICE_OPTIONS_OFFSET_X 1
#define UI_DUAL_GIF_STORY_CHOICE_OPTIONS_OFFSET_Y -1
#define UI_DUAL_GIF_STORY_CHOICE_BUTTON_WIDTH  362
#define UI_DUAL_GIF_STORY_CHOICE_BUTTON_HEIGHT  83
#define UI_DUAL_GIF_STORY_CHOICE_FADE_IN_MS    500U
#define UI_DUAL_GIF_STORY_CHOICE_FADE_OUT_MS   150U
#define UI_DUAL_GIF_STORY_CHOICE_FADE_IN_DELAY_MS  0U
#define UI_DUAL_GIF_STORY_CHOICE_FADE_OUT_DELAY_MS 0U
#define UI_DUAL_GIF_STORY_CHOICE_TOP_PAD         15
#define UI_DUAL_GIF_STORY_CHOICE_ROW_PAD       18
#define UI_DUAL_GIF_POST_CLOSE_PANEL_COUNT      8U
#define UI_DUAL_GIF_POST_CLOSE_PANEL_SIZE      50
#define UI_DUAL_GIF_POST_CLOSE_PANEL_RADIUS   100
#define UI_DUAL_GIF_POST_CLOSE_PANEL_SHADOW_WIDTH 40
#define UI_DUAL_GIF_DECOR_BG_OFFSET_X          0
#define UI_DUAL_GIF_DECOR_BG_OFFSET_Y          0
#define UI_DUAL_GIF_DECOR_BG_WIDTH             BSP_LCD_H_RES
#define UI_DUAL_GIF_DECOR_BG_HEIGHT            320
#define UI_DUAL_GIF_SETTINGS_VOLUME_MIN        0
#define UI_DUAL_GIF_SETTINGS_VOLUME_MAX        100
#define UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MIN    10
#define UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MAX    BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX
#define UI_DUAL_GIF_SETTINGS_NAME_EMPTY        "-"
#define UI_DUAL_GIF_SETTINGS_TOAST_DURATION_MS 1500U
#define UI_DUAL_GIF_SETTINGS_TOAST_FADE_MS      220U
#define UI_DUAL_GIF_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#ifndef CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES
#define CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES 20971520
#endif

#ifndef CONFIG_FAIRYTALE_ANIM_CFA_FREE_PSRAM_MARGIN_BYTES
#define CONFIG_FAIRYTALE_ANIM_CFA_FREE_PSRAM_MARGIN_BYTES 6291456
#endif

typedef struct {
    uint32_t frame_count;
    uint32_t missed_deadlines;
    uint32_t local_palette_frames;
    uint32_t scheduled_delay_count;
    uint32_t scheduled_delay_ms_total;
    uint32_t scheduled_delay_ms_min;
    uint32_t scheduled_delay_ms_max;
    uint32_t clamped_delay_frames;
    uint64_t gd_get_frame_us_total;
    uint64_t gd_get_frame_us_max;
    uint64_t pixel_update_us_total;
    uint64_t pixel_update_us_max;
    uint64_t lvgl_lock_us_total;
    uint64_t lvgl_lock_us_max;
    uint64_t cfa_read_us_total;
    uint64_t cfa_read_us_max;
    uint64_t cfa_crc_us_total;
    uint64_t cfa_crc_us_max;
    uint64_t cfa_decompress_us_total;
    uint64_t cfa_decompress_us_max;
    uint64_t cfa_total_us_total;
    uint64_t cfa_total_us_max;
    uint64_t cfa_raw_size_total;
    uint64_t cfa_payload_size_total;
    uint64_t cfa_rect_pixels_total;
    uint32_t cfa_raw_size_max;
    uint32_t cfa_payload_size_max;
    uint32_t cfa_rect_pixels_max;
    uint32_t cfa_codec_frames[3];
    uint32_t cfa_preloaded_frames;
    uint32_t cfa_streaming_frames;
    uint64_t present_interval_us_total;
    uint64_t present_interval_us_max;
    uint64_t playback_started_us;
    uint64_t last_present_us;
} ui_dual_gif_stats_t;

typedef struct ui_dual_gif_player_t ui_dual_gif_player_t;

typedef struct {
    ui_dual_gif_player_t *player;
    const char *path;
    size_t sequence_index;
    uint32_t generation;
} ui_dual_gif_preload_job_t;

typedef struct {
    const char *path;
    const char *const *sequence_paths;
    size_t sequence_count;
    bool repeat;
    bool hold_last_frame;
} ui_dual_gif_bird_state_cfg_t;

typedef struct {
    lv_coord_t x;
    lv_coord_t y;
    lv_coord_t shadow_spread;
} ui_dual_gif_post_close_panel_cfg_t;

typedef enum {
    UI_DUAL_GIF_DECOR_ASSET_STAR_MID = 0,
    UI_DUAL_GIF_DECOR_ASSET_STAR_MINI,
    UI_DUAL_GIF_DECOR_ASSET_SPARK_BIG,
    UI_DUAL_GIF_DECOR_ASSET_SPARK_MID,
    UI_DUAL_GIF_DECOR_ASSET_SPARK_MINI,
    UI_DUAL_GIF_DECOR_ASSET_COUNT
} ui_dual_gif_decor_asset_id_t;

typedef struct {
    const char *spiffs_path;
    uint32_t raw_size;
    uint8_t *blob;
    lv_img_dsc_t img_dsc;
} ui_dual_gif_decor_asset_t;

typedef struct {
    lv_coord_t width;
    lv_coord_t height;
    lv_coord_t x;
    lv_coord_t y;
    lv_coord_t shadow_spread;
} ui_dual_gif_decor_shadow_cfg_t;

typedef struct {
    ui_dual_gif_decor_asset_id_t asset_id;
    lv_coord_t x;
    lv_coord_t y;
    int16_t angle;
    bool set_angle;
} ui_dual_gif_decor_img_cfg_t;

typedef enum {
    UI_DUAL_GIF_BOOK_ACTION_NONE = 0,
    UI_DUAL_GIF_BOOK_ACTION_OPEN,
    UI_DUAL_GIF_BOOK_ACTION_SWIPE,
    UI_DUAL_GIF_BOOK_ACTION_CLOSE,
} ui_dual_gif_book_action_t;

typedef enum {
    UI_DUAL_GIF_SETTINGS_CONFIRM_NONE = 0,
    UI_DUAL_GIF_SETTINGS_CONFIRM_RESTART,
    UI_DUAL_GIF_SETTINGS_CONFIRM_CLEAR_NAME,
} ui_dual_gif_settings_confirm_action_t;

typedef enum {
    UI_DUAL_GIF_CTRL_CMD_RESET_STORY = 0,
    UI_DUAL_GIF_CTRL_CMD_START_OPEN,
    UI_DUAL_GIF_CTRL_CMD_START_SWIPE,
    UI_DUAL_GIF_CTRL_CMD_START_CLOSE,
    UI_DUAL_GIF_CTRL_CMD_SET_BIRD_MODE,
} ui_dual_gif_ctrl_cmd_type_t;

typedef struct {
    ui_dual_gif_ctrl_cmd_type_t type;
    uint8_t value;
} ui_dual_gif_ctrl_cmd_t;

struct ui_dual_gif_player_t {
    const char *path;
    const char *const *sequence_paths;
    size_t sequence_count;
    size_t sequence_index;
    bool repeat;
    bool invalidate_full_frame;
    bool render_under_lvgl_lock;
    bool use_true_color_alpha;
    bool animate_entry;
    bool entry_anim_started;
    bool source_attached;
    lv_align_t align;
    lv_coord_t align_x;
    lv_coord_t align_y;
    lv_coord_t entry_start_shift_x;
    lv_coord_t entry_start_shift_y;
    uint32_t entry_delay_ms;
    uint32_t entry_duration_ms;
    uint32_t return_delay_ms;
    uint32_t return_duration_ms;
    uint16_t render_w;
    uint16_t render_h;
    animation_decoder_t *gif;
    uint8_t *img_buf;
    size_t img_buf_size;
    size_t img_buf_capacity;
    uint8_t *render_buf;
    size_t render_buf_size;
    size_t render_buf_capacity;
    lv_color_t *lv_buf;
    uint8_t *alpha_buf;
    lv_color_t pal565[256];
    const uint8_t *pal565_source;
    lv_color_t *restore_color_buf;
    uint8_t *restore_alpha_buf;
    uint32_t restore_buf_px_capacity;
    bool restore_snapshot_valid;
    lv_area_t restore_area;
    TaskHandle_t task;
    UBaseType_t task_priority;
    SemaphoreHandle_t task_done_sem;
    uint32_t preload_generation;
    bool preload_enqueued;
    const char *preload_target_path;
    size_t preload_target_index;
    const char *preloaded_path;
    size_t preloaded_sequence_index;
    animation_decoder_t *preloaded_gif;
    size_t preloaded_file_size;
    volatile bool stop_requested;
    bool local_palette_warned;
    bool prev_rect_valid;
    uint8_t prev_disposal;
    lv_area_t prev_frame_area;
    uint32_t src_px_count;
    uint32_t render_px_count;
    lv_img_dsc_t img_dsc;
    lv_obj_t *img_obj;
    ui_dual_gif_stats_t stats;
    volatile bool bird_state_dirty;
    volatile uint8_t requested_bird_state;
    uint8_t active_bird_state;
    bool hold_last_frame_on_finish;
    bool waiting_for_bird_state_change;
    uint32_t scheduled_delay_ms_pending;
};

static const char *TAG = "dual_gif_ui";

#if CONFIG_FAIRYTALE_VERBOSE_RUNTIME_LOGS
#define UI_DIAG_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define UI_DIAG_LOGI(...) do { } while (0)
#endif
extern const lv_img_dsc_t ui_img_corner_small_crop_png;
extern const lv_img_dsc_t ui_img_bright_min_png;
extern const lv_img_dsc_t ui_img_bright_png;
extern const lv_img_dsc_t ui_img_name_png;
extern const lv_img_dsc_t ui_img_restart_png;
extern const lv_img_dsc_t ui_img_sett_off_png;
extern const lv_img_dsc_t ui_img_sett_on_png;
extern const lv_img_dsc_t ui_img_vol_min_png;
extern const lv_img_dsc_t ui_img_vol_png;
extern const lv_font_t ui_font_Font1;
extern const lv_font_t ui_font_Font11;
extern const lv_font_t ui_font_Font4;
extern const lv_font_t ui_font_Font3;
extern const lv_font_t ui_font_Font6;
extern const lv_font_t ui_font_Font22;
extern const lv_font_t ui_font_Font33;
extern const lv_font_t ui_font_Font51;
extern const lv_font_t ui_font_Font52;

static const char s_post_close_label_text[] = "Restart Story";
static const ui_dual_gif_post_close_panel_cfg_t s_post_close_panel_cfgs[UI_DUAL_GIF_POST_CLOSE_PANEL_COUNT] = {
    { -369, -205, 25 },
    { -317, -209, 25 },
    { -297, -231, 25 },
    { -314, -149, 20 },
    { -375, -155, 25 },
    { -260, -171, 20 },
    { -202, -210, 25 },
    { -246, -225, 25 },
};
static ui_dual_gif_decor_asset_t s_decor_assets[UI_DUAL_GIF_DECOR_ASSET_COUNT] = {
    [UI_DUAL_GIF_DECOR_ASSET_STAR_MID] = {
        .spiffs_path = "S:/assets/screen_stars/star_mid.bin",
        .raw_size = 7504U,
    },
    [UI_DUAL_GIF_DECOR_ASSET_STAR_MINI] = {
        .spiffs_path = "S:/assets/screen_stars/star_mini.bin",
        .raw_size = 2704U,
    },
    [UI_DUAL_GIF_DECOR_ASSET_SPARK_BIG] = {
        .spiffs_path = "S:/assets/screen_stars/spark_big.bin",
        .raw_size = 7504U,
    },
    [UI_DUAL_GIF_DECOR_ASSET_SPARK_MID] = {
        .spiffs_path = "S:/assets/screen_stars/spark_mid.bin",
        .raw_size = 2704U,
    },
    [UI_DUAL_GIF_DECOR_ASSET_SPARK_MINI] = {
        .spiffs_path = "S:/assets/screen_stars/spark_mini.bin",
        .raw_size = 1204U,
    },
};
static const ui_dual_gif_decor_shadow_cfg_t s_decor_star_shadow_cfgs[] = {
    { 10, 10, -363, -220, 9 },
    { 5, 5, -464, -167, 7 },
    { 5, 5, -243, -200, 7 },
    { 10, 10, 462, -153, 9 },
    { 5, 5, 472, -236, 7 },
    { 5, 5, 303, -178, 7 },
};
static const ui_dual_gif_decor_img_cfg_t s_decor_star_cfgs[] = {
    { UI_DUAL_GIF_DECOR_ASSET_STAR_MID, -362, -222, 100, true },
    { UI_DUAL_GIF_DECOR_ASSET_STAR_MINI, -464, -168, 9, true },
    { UI_DUAL_GIF_DECOR_ASSET_STAR_MINI, -243, -201, 9, true },
    { UI_DUAL_GIF_DECOR_ASSET_STAR_MID, 462, -158, 100, true },
    { UI_DUAL_GIF_DECOR_ASSET_STAR_MINI, 472, -236, 9, true },
    { UI_DUAL_GIF_DECOR_ASSET_STAR_MINI, 303, -178, 9, true },
};
static const ui_dual_gif_decor_img_cfg_t s_decor_spark_cfgs[] = {
    { UI_DUAL_GIF_DECOR_ASSET_SPARK_MINI, -429, -228, 0, false },
    { UI_DUAL_GIF_DECOR_ASSET_SPARK_MID, -250, -254, -90, true },
    { UI_DUAL_GIF_DECOR_ASSET_SPARK_BIG, -397, -150, 0, false },
    { UI_DUAL_GIF_DECOR_ASSET_SPARK_MID, -322, -169, -90, true },
    { UI_DUAL_GIF_DECOR_ASSET_SPARK_MID, -461, -48, -90, true },
    { UI_DUAL_GIF_DECOR_ASSET_SPARK_BIG, 372, -263, 90, true },
    { UI_DUAL_GIF_DECOR_ASSET_SPARK_MINI, 465, -63, 0, false },
    { UI_DUAL_GIF_DECOR_ASSET_SPARK_MID, 407, -199, -90, true },
    { UI_DUAL_GIF_DECOR_ASSET_SPARK_MID, 376, -116, 90, true },
};

static lv_obj_t *s_dual_gif_screen = NULL;
static lv_obj_t *s_decor_bg_canvas = NULL;
static lv_obj_t *s_book_img = NULL;
static lv_obj_t *s_bird_img = NULL;
static lv_obj_t *s_book_overlay_layer = NULL;
static lv_obj_t *s_a_small_img = NULL;
static lv_obj_t *s_story_text_view = NULL;
static lv_obj_t *s_story_text_label = NULL;
static lv_obj_t *s_story_choice_container = NULL;
static lv_obj_t *s_story_choice_question_panel = NULL;
static lv_obj_t *s_story_choice_question_label = NULL;
static lv_obj_t *s_story_choice_options_container = NULL;
static lv_obj_t *s_story_choice_button1 = NULL;
static lv_obj_t *s_story_choice_button1_label = NULL;
static lv_obj_t *s_story_choice_button2 = NULL;
static lv_obj_t *s_story_choice_button2_label = NULL;
static lv_obj_t *s_story_choice_button2_disabled_tint = NULL;
static lv_obj_t *s_story_img_button = NULL;
static lv_obj_t *s_settings_screen = NULL;
static lv_obj_t *s_settings_close_button = NULL;
static lv_obj_t *s_settings_volume_icon = NULL;
static lv_obj_t *s_settings_volume_slider = NULL;
static lv_obj_t *s_settings_brightness_icon = NULL;
static lv_obj_t *s_settings_brightness_slider = NULL;
static lv_obj_t *s_settings_user_name_value_label = NULL;
static lv_obj_t *s_settings_restart_button = NULL;
static lv_obj_t *s_settings_clear_name_button = NULL;
static lv_obj_t *s_settings_confirm_overlay = NULL;
static lv_obj_t *s_settings_toast = NULL;
static lv_obj_t *s_settings_toast_label = NULL;
static lv_obj_t *s_post_close_panels[UI_DUAL_GIF_POST_CLOSE_PANEL_COUNT] = { NULL };
static lv_obj_t *s_post_close_label = NULL;
static lv_obj_t *s_corner1_img = NULL;
static lv_obj_t *s_corner2_img = NULL;
static lv_obj_t *s_corner3_img = NULL;
static lv_obj_t *s_corner4_img = NULL;
static lv_timer_t *s_overlay_swipe_hide_timer = NULL;
static lv_timer_t *s_overlay_swipe_show_timer = NULL;
static lv_timer_t *s_overlay_hide_timer = NULL;
static lv_timer_t *s_story_text_fade_in_timer = NULL;
static lv_timer_t *s_story_text_fade_out_timer = NULL;
static lv_timer_t *s_story_text_auto_scroll_timer = NULL;
static lv_timer_t *s_story_choice_fade_in_timer = NULL;
static lv_timer_t *s_story_choice_fade_out_timer = NULL;
static lv_timer_t *s_bird_frame_timer = NULL;
static lv_timer_t *s_book_preload_defer_timer = NULL;
static lv_timer_t *s_settings_toast_timer = NULL;
static uint8_t *s_decor_bg_buf = NULL;
static lv_coord_t s_story_text_auto_scroll_offset = 0;
static const lv_img_dsc_t *s_overlay_story_img = NULL;
static const lv_img_dsc_t *s_default_overlay_img = NULL;
static ui_dual_gif_story_content_t s_staged_story_content = {0};
static uint64_t s_book_animation_started_us = 0;
static ui_dual_gif_bird_state_t s_bird_state = UI_DUAL_GIF_BIRD_INITIAL_STATE;
static volatile bool s_story_choice_waiting = false;
static ui_dual_gif_book_action_t s_book_action = UI_DUAL_GIF_BOOK_ACTION_NONE;
static bool s_show_choice_after_text_fade = false;
static bool s_audio_active = false;
static const char *s_deferred_book_preload_path = NULL;
static bool s_scene_ready_pending = false;
static bool s_scene_overlay_ready = false;
static bool s_book_transition_ready = false;
static bool s_current_scene_is_final = false;
static bool s_first_open_text_delay_pending = true;
static ui_dual_gif_settings_confirm_action_t s_settings_confirm_action = UI_DUAL_GIF_SETTINGS_CONFIRM_NONE;
static ui_dual_gif_screen_callbacks_t s_callbacks = {0};
static QueueHandle_t s_control_queue = NULL;
static TaskHandle_t s_control_task = NULL;
static media_cache_slot_t s_animation_cache_slots[4] = {0};
static media_cache_t s_animation_cache = {0};
static size_t s_animation_cfa_cache_bytes = 0U;
static bool s_initial_media_preload_complete = false;
static const char *const s_book_open_sequence_paths[] = {
    UI_DUAL_GIF_OPEN_PATH,
};
static const char *const s_book_swipe_sequence_paths[] = {
    UI_DUAL_GIF_SWIPE_PATH,
};
static const char *const s_book_close_sequence_paths[] = {
    UI_DUAL_GIF_CLOSE_PATH,
};
static const char *const s_bird_story_sequence_paths[] = {
    UI_DUAL_GIF_BIRD_START_PATH,
};
static const char *const s_bird_talk_loop_sequence_paths[] = {
    UI_DUAL_GIF_BIRD_LOOP_PATH,
};
static const char *const s_bird_idle_test_sequence_paths[] = {
    UI_DUAL_GIF_BIRD_IDLE_TEST_PATH,
};

static const ui_dual_gif_bird_state_cfg_t s_bird_pre_talk_state_cfg = {
    .path = UI_DUAL_GIF_BIRD_START_PATH,
    .sequence_paths = s_bird_story_sequence_paths,
    .sequence_count = sizeof(s_bird_story_sequence_paths) / sizeof(s_bird_story_sequence_paths[0]),
    .repeat = false,
    .hold_last_frame = true,
};

static const ui_dual_gif_bird_state_cfg_t s_bird_talk_loop_state_cfg = {
    .path = UI_DUAL_GIF_BIRD_LOOP_PATH,
    .sequence_paths = s_bird_talk_loop_sequence_paths,
    .sequence_count = sizeof(s_bird_talk_loop_sequence_paths) / sizeof(s_bird_talk_loop_sequence_paths[0]),
    .repeat = true,
    .hold_last_frame = false,
};

static const ui_dual_gif_bird_state_cfg_t s_bird_idle_state_cfg = {
    .path = UI_DUAL_GIF_BIRD_IDLE_TEST_PATH,
    .sequence_paths = s_bird_idle_test_sequence_paths,
    .sequence_count = sizeof(s_bird_idle_test_sequence_paths) / sizeof(s_bird_idle_test_sequence_paths[0]),
    .repeat = true,
    .hold_last_frame = false,
};

static ui_dual_gif_player_t s_book_player = {
    .path = UI_DUAL_GIF_OPEN_PATH,
    .sequence_paths = s_book_open_sequence_paths,
    .sequence_count = sizeof(s_book_open_sequence_paths) / sizeof(s_book_open_sequence_paths[0]),
    .sequence_index = 0,
    .repeat = false,
    .invalidate_full_frame = false,
    .render_under_lvgl_lock = true,
    .use_true_color_alpha = false,
    .animate_entry = false,
    .task_priority = UI_DUAL_GIF_BOOK_TASK_PRIORITY,
    .align = LV_ALIGN_CENTER,
    .align_x = UI_DUAL_GIF_BOOK_OFFSET_X,
    .align_y = UI_DUAL_GIF_BOOK_OFFSET_Y,
};

static ui_dual_gif_player_t s_bird_player = {
    .path = UI_DUAL_GIF_BIRD_IDLE_TEST_PATH,
    .sequence_paths = s_bird_idle_test_sequence_paths,
    .sequence_count = sizeof(s_bird_idle_test_sequence_paths) / sizeof(s_bird_idle_test_sequence_paths[0]),
    .sequence_index = 0,
    .repeat = true,
    .invalidate_full_frame = true,
    .render_under_lvgl_lock = true,
    .use_true_color_alpha = false,
    .animate_entry = true,
    .task_priority = UI_DUAL_GIF_BIRD_TASK_PRIORITY,
    .align = LV_ALIGN_TOP_RIGHT,
    .align_x = UI_DUAL_GIF_BIRD_OFFSET_X,
    .align_y = UI_DUAL_GIF_BIRD_OFFSET_Y,
    .entry_start_shift_x = UI_DUAL_GIF_BIRD_ENTRY_START_SHIFT_X,
    .entry_start_shift_y = UI_DUAL_GIF_BIRD_ENTRY_START_SHIFT_Y,
    .entry_delay_ms = UI_DUAL_GIF_BIRD_ENTRY_DELAY_MS,
    .entry_duration_ms = UI_DUAL_GIF_BIRD_ENTRY_DURATION_MS,
    .return_delay_ms = UI_DUAL_GIF_BIRD_RETURN_DELAY_MS,
    .return_duration_ms = UI_DUAL_GIF_BIRD_RETURN_DURATION_MS,
    .requested_bird_state = UI_DUAL_GIF_BIRD_INITIAL_STATE,
    .active_bird_state = UI_DUAL_GIF_BIRD_INITIAL_STATE,
};

static bool ui_dual_gif_switch_to_sequence(ui_dual_gif_player_t *player, size_t next_index);
static bool ui_dual_gif_render_current_frame(ui_dual_gif_player_t *player,
                                             lv_area_t *invalidate_area,
                                             uint64_t *pixel_update_us,
                                             bool full_init);
static bool ui_dual_gif_render_current_cfa_frame(ui_dual_gif_player_t *player,
                                                 lv_area_t *invalidate_area,
                                                 uint64_t *pixel_update_us,
                                                 bool full_init);
static void ui_dual_gif_attach_source(ui_dual_gif_player_t *player);
static void ui_dual_gif_pack_true_color_alpha(ui_dual_gif_player_t *player);
static void ui_dual_gif_sync_rgb565a8_area(ui_dual_gif_player_t *player, const lv_area_t *area);
static void ui_dual_gif_invalidate_area(ui_dual_gif_player_t *player, const lv_area_t *area);
static void ui_dual_gif_align_story_layers(void);
static animation_decoder_t *ui_dual_gif_take_cached_animation(const char *path, size_t *out_file_size);
static bool ui_dual_gif_has_cached_animation(const char *path);
static void ui_dual_gif_store_cached_animation(const char *path, animation_decoder_t *gif, size_t file_size);
static void ui_dual_gif_purge_book_cfa_cache_except(const char *keep_path);
static void ui_dual_gif_control_task(void *arg);
static bool ui_dual_gif_control_init(void);
static bool ui_dual_gif_post_control_command(ui_dual_gif_ctrl_cmd_type_t type, uint8_t value);
static void ui_dual_gif_reset_story_state_internal(bool keep_active_open_cfa);
static void ui_dual_gif_configure_book_action(ui_dual_gif_book_action_t action);
static void ui_dual_gif_reset_story_state_now(void);
static void ui_dual_gif_start_open_now(void);
static void ui_dual_gif_start_swipe_now(void);
static void ui_dual_gif_start_close_now(void);
static void ui_dual_gif_apply_story_text_content_now(const ui_dual_gif_story_content_t *content, bool force_reset);
static void ui_dual_gif_apply_story_content_now(const ui_dual_gif_story_content_t *content);
static void ui_dual_gif_set_obj_translate_x(void *obj, int32_t value);
static void ui_dual_gif_set_obj_translate_y(void *obj, int32_t value);
static void ui_dual_gif_start_entry_animation(ui_dual_gif_player_t *player, uint32_t delay_ms);
static void ui_dual_gif_try_start_bird_entry_animation(void);
static void ui_dual_gif_start_bird_return_animation(void);
static void ui_dual_gif_finish_bird_return_height(void);
static void ui_dual_gif_mark_book_animation_started(void);
static void ui_dual_gif_schedule_next_preload(ui_dual_gif_player_t *player);
static void ui_dual_gif_stop_player_task(ui_dual_gif_player_t *player, uint32_t wait_ms);
static void ui_dual_gif_close_gif_internal(ui_dual_gif_player_t *player, bool allow_cache);
static void ui_dual_gif_prepare_player_reopen_for_path(ui_dual_gif_player_t *player, const char *next_path);
static void ui_dual_gif_free_player(ui_dual_gif_player_t *player);
static bool ui_dual_gif_attach_current_frame(ui_dual_gif_player_t *player);
static bool ui_dual_gif_attach_current_frame_on_ui_thread(ui_dual_gif_player_t *player);
static void ui_dual_gif_start_player(ui_dual_gif_player_t *player);
static void ui_dual_gif_stop_bird_timer(void);
static void ui_dual_gif_arm_bird_timer(uint32_t delay_ms);
static void ui_dual_gif_bird_timer_cb(lv_timer_t *timer);
static bool ui_dual_gif_present_frame_on_ui_thread(ui_dual_gif_player_t *player,
                                                   bool full_init,
                                                   uint64_t *pixel_update_us);
static bool ui_dual_gif_set_bird_state_now(ui_dual_gif_bird_state_t state);
static const ui_dual_gif_bird_state_cfg_t *ui_dual_gif_get_bird_state_cfg(ui_dual_gif_bird_state_t state);
static void ui_dual_gif_apply_bird_state_config(ui_dual_gif_player_t *player, ui_dual_gif_bird_state_t state);
static void ui_dual_gif_reset_bird_player_state(ui_dual_gif_player_t *player, ui_dual_gif_bird_state_t state);
static bool ui_dual_gif_request_bird_state(ui_dual_gif_bird_state_t state);
static bool ui_dual_gif_apply_requested_bird_state(ui_dual_gif_player_t *player, TickType_t *last_wake);
static void ui_dual_gif_set_obj_img_opa(void *obj, int32_t value);
static void ui_dual_gif_cancel_overlay_swipe_timers(void);
static void ui_dual_gif_cancel_overlay_hide_timer(void);
static void ui_dual_gif_cancel_overlay_corner_anims(void);
static const lv_img_dsc_t *ui_dual_gif_get_default_overlay_image(void);
static void ui_dual_gif_log_overlay_runtime_state(const char *stage, const ui_dual_gif_story_content_t *content);
static void ui_dual_gif_force_overlay_redraw(const char *stage);
static void ui_dual_gif_release_a_small_bin(void);
static bool ui_dual_gif_load_decor_asset(ui_dual_gif_decor_asset_t *asset);
static const lv_img_dsc_t *ui_dual_gif_get_decor_asset(ui_dual_gif_decor_asset_id_t asset_id);
static void ui_dual_gif_release_decor_assets(void);
static void ui_dual_gif_set_overlay_story_image(const lv_img_dsc_t *img_dsc);
static void ui_dual_gif_set_overlay_corners_opa(lv_opa_t value);
static lv_opa_t ui_dual_gif_get_overlay_corners_opa(void);
static void ui_dual_gif_start_overlay_corners_fade(int32_t from, int32_t to, uint32_t duration_ms, uint32_t delay_ms);
static void ui_dual_gif_finish_overlay_hide(void);
static void ui_dual_gif_swipe_overlay_hide_timer_cb(lv_timer_t *timer);
static void ui_dual_gif_swipe_overlay_show_timer_cb(lv_timer_t *timer);
static void ui_dual_gif_reset_overlay_state(void);
static void ui_dual_gif_start_overlay_stage_sequence(const lv_img_dsc_t *story_img,
                                                     uint32_t show_delay_ms,
                                                     uint32_t hide_delay_ms,
                                                     bool schedule_hide,
                                                     bool show_story_text);
static void ui_dual_gif_hide_overlay(void);
static void ui_dual_gif_layout_story_text(void);
static void ui_dual_gif_hide_story_text(void);
static void ui_dual_gif_set_obj_opa(void *obj, int32_t value);
static void ui_dual_gif_cancel_story_text_timers(void);
static void ui_dual_gif_cancel_story_text_anim(void);
static void ui_dual_gif_cancel_story_text_auto_scroll(void);
static void ui_dual_gif_maybe_start_story_text_auto_scroll(void);
static void ui_dual_gif_story_text_auto_scroll_timer_cb(lv_timer_t *timer);
static void ui_dual_gif_story_text_fade_in_timer_cb(lv_timer_t *timer);
static void ui_dual_gif_story_text_fade_out_timer_cb(lv_timer_t *timer);
static void ui_dual_gif_schedule_story_text_fade_in(uint32_t delay_ms);
static void ui_dual_gif_schedule_story_text_fade_out(uint32_t delay_ms);
static void ui_dual_gif_story_text_fade_out_ready_cb(lv_anim_t *anim);
static void ui_dual_gif_start_story_text_fade_in_now(void);
static void ui_dual_gif_start_story_text_fade_out_now(void);
static void ui_dual_gif_cancel_story_choice_timers(void);
static void ui_dual_gif_cancel_story_choice_anim(void);
static void ui_dual_gif_story_choice_fade_in_timer_cb(lv_timer_t *timer);
static void ui_dual_gif_story_choice_fade_out_timer_cb(lv_timer_t *timer);
static void ui_dual_gif_schedule_story_choice_fade_in(uint32_t delay_ms);
static void ui_dual_gif_schedule_story_choice_fade_out(uint32_t delay_ms);
static void ui_dual_gif_story_choice_fade_out_ready_cb(lv_anim_t *anim);
static void ui_dual_gif_start_story_choice_fade_in_now(void);
static void ui_dual_gif_start_story_choice_fade_out_now(void);
static void ui_dual_gif_hide_cloud(void);
static void ui_dual_gif_make_cloud_invisible(void);
static void ui_dual_gif_show_cloud(void);
static void ui_dual_gif_cancel_cloud_anim(void);
static void ui_dual_gif_create_post_close_panels(void);
static void ui_dual_gif_draw_decor_shadow(const ui_dual_gif_decor_shadow_cfg_t *cfg);
static void ui_dual_gif_draw_decor_img(const ui_dual_gif_decor_img_cfg_t *cfg);
static void ui_dual_gif_create_background_decor(void);
static void ui_dual_gif_decor_canvas_delete_event_cb(lv_event_t *e);
static void ui_dual_gif_hide_story_choice(void);
static void ui_dual_gif_show_story_choice(void);
static void ui_dual_gif_hide_story_img_button(void);
static void ui_dual_gif_show_story_img_button(void);
static int ui_dual_gif_clamp_int(int value, int min_value, int max_value);
static void ui_dual_gif_refresh_settings_volume_icon(int volume);
static void ui_dual_gif_refresh_settings_brightness_icon(int brightness);
static void ui_dual_gif_refresh_settings_user_name(void);
static void ui_dual_gif_refresh_settings_controls(void);
static void ui_dual_gif_style_settings_slider(lv_obj_t *slider);
static void ui_dual_gif_style_yellow_button_pressed_shadow(lv_obj_t *button);
static void ui_dual_gif_style_light_button_pressed_shadow(lv_obj_t *button);
static void ui_dual_gif_settings_persist_volume(int volume);
static void ui_dual_gif_settings_persist_brightness(int brightness);
static void ui_dual_gif_dismiss_settings_confirm(void);
static void ui_dual_gif_show_settings_confirm(const char *title,
                                              const char *body,
                                              const char *confirm_text,
                                              ui_dual_gif_settings_confirm_action_t action);
static void ui_dual_gif_hide_settings_toast(void);
static void ui_dual_gif_settings_toast_fade_out_ready_cb(lv_anim_t *anim);
static void ui_dual_gif_show_settings_toast(const char *text);
static void ui_dual_gif_show_settings_screen(void);
static void ui_dual_gif_hide_settings_screen(void);
static void ui_dual_gif_settings_open_event_cb(lv_event_t *e);
static void ui_dual_gif_settings_close_event_cb(lv_event_t *e);
static void ui_dual_gif_settings_restart_event_cb(lv_event_t *e);
static void ui_dual_gif_settings_clear_name_event_cb(lv_event_t *e);
static void ui_dual_gif_settings_volume_slider_event_cb(lv_event_t *e);
static void ui_dual_gif_settings_brightness_slider_event_cb(lv_event_t *e);
static void ui_dual_gif_story_choice_event_cb(lv_event_t *e);
static void ui_dual_gif_cloud_button_event_cb(lv_event_t *e);
static bool ui_dual_gif_preload_path(ui_dual_gif_player_t *player, const char *path);
static bool ui_dual_gif_preload_job_run(void *ctx, uint32_t generation);
static bool ui_dual_gif_preload_job_stale(void *ctx, uint32_t generation);
static void ui_dual_gif_preload_job_free(void *ctx);
static void ui_dual_gif_cancel_deferred_book_preload(void);
static bool ui_dual_gif_book_preload_audio_active(void);
static bool ui_dual_gif_preload_book_path_now(const char *path);
static bool ui_dual_gif_defer_book_preload_until_audio_stable(const char *path);
static void ui_dual_gif_configure_book_action(ui_dual_gif_book_action_t action);
static void ui_dual_gif_notify_scene_image_ready(void);
static void ui_dual_gif_try_notify_scene_image_ready(void *ctx);
static void ui_dual_gif_notify_close_finished_on_ui_thread(void *ctx);
static void ui_dual_gif_release_screen_state(void);
static void ui_dual_gif_screen_delete_event_cb(lv_event_t *e);

static uint32_t ui_dual_gif_frame_delay_ms(const animation_decoder_t *gif)
{
    return animation_decoder_frame_delay_ms(gif);
}

static uint16_t ui_dual_gif_frame_delay_cs(const animation_decoder_t *gif)
{
    return animation_decoder_frame_delay_cs(gif);
}

static bool ui_dual_gif_paths_match(const char *lhs, const char *rhs)
{
    if (lhs == rhs) {
        return true;
    }

    if (!lhs || !rhs) {
        return false;
    }

    return strcmp(lhs, rhs) == 0;
}

static bool ui_dual_gif_decoder_is_cfa(const animation_decoder_t *decoder)
{
    return decoder && animation_decoder_kind(decoder) == ANIMATION_DECODER_KIND_CFA;
}

static bool ui_dual_gif_path_is_cfa(const char *path)
{
    const char *ext = NULL;

    if (!path) {
        return false;
    }

    ext = strrchr(path, '.');
    return ext && strcasecmp(ext, ".cfa") == 0;
}

static bool ui_dual_gif_cfa_decoder_is_preloaded(const animation_decoder_t *decoder)
{
    return ui_dual_gif_decoder_is_cfa(decoder) && animation_decoder_is_preloaded(decoder);
}

static void ui_dual_gif_subtract_cfa_cache_bytes(size_t file_size)
{
    if (file_size == 0U) {
        return;
    }

    if (s_animation_cfa_cache_bytes >= file_size) {
        s_animation_cfa_cache_bytes -= file_size;
    } else {
        s_animation_cfa_cache_bytes = 0U;
    }
}

static bool ui_dual_gif_cfa_cache_can_store(size_t file_size)
{
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    if (file_size == 0U || CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES == 0U) {
        return false;
    }
    if (file_size > CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES) {
        return false;
    }
    if (s_animation_cfa_cache_bytes > CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES ||
        file_size > CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES - s_animation_cfa_cache_bytes) {
        return false;
    }

    return free_psram > CONFIG_FAIRYTALE_ANIM_CFA_FREE_PSRAM_MARGIN_BYTES;
}

static void ui_dual_gif_mark_area_invalid(lv_area_t *area)
{
    if (!area) {
        return;
    }

    area->x1 = 1;
    area->y1 = 0;
    area->x2 = 0;
    area->y2 = 0;
}

static const ui_dual_gif_bird_state_cfg_t *ui_dual_gif_get_bird_state_cfg(ui_dual_gif_bird_state_t state)
{
    switch (state) {
    case UI_DUAL_GIF_BIRD_STATE_IDLE:
        return &s_bird_idle_state_cfg;

    case UI_DUAL_GIF_BIRD_STATE_PRE_TALK:
        return &s_bird_pre_talk_state_cfg;

    case UI_DUAL_GIF_BIRD_STATE_TALK_LOOP:
        return &s_bird_talk_loop_state_cfg;

    default:
        return NULL;
    }
}

static void ui_dual_gif_apply_bird_state_config(ui_dual_gif_player_t *player, ui_dual_gif_bird_state_t state)
{
    const ui_dual_gif_bird_state_cfg_t *cfg = ui_dual_gif_get_bird_state_cfg(state);

    if (!player || !cfg) {
        return;
    }

    player->path = cfg->path;
    player->sequence_paths = cfg->sequence_paths;
    player->sequence_count = cfg->sequence_count;
    player->sequence_index = 0;
    player->repeat = cfg->repeat;
    player->hold_last_frame_on_finish = cfg->hold_last_frame;
    player->waiting_for_bird_state_change = false;
}

static void ui_dual_gif_reset_bird_player_state(ui_dual_gif_player_t *player, ui_dual_gif_bird_state_t state)
{
    if (!player) {
        return;
    }

    s_bird_state = state;
    ui_dual_gif_apply_bird_state_config(player, state);
    player->requested_bird_state = (uint8_t)state;
    player->active_bird_state = (uint8_t)state;
    player->bird_state_dirty = false;
}

static void ui_dual_gif_release_cached_animation_value(void *value)
{
    animation_decoder_t *decoder = (animation_decoder_t *)value;

    if (ui_dual_gif_decoder_is_cfa(decoder)) {
        ui_dual_gif_subtract_cfa_cache_bytes(animation_decoder_file_size(decoder));
    }

    animation_decoder_close(decoder);
}

static animation_decoder_t *ui_dual_gif_take_cached_animation(const char *path, size_t *out_file_size)
{
    animation_decoder_t *decoder = NULL;

    if (!path) {
        return NULL;
    }

    if (!media_cache_take(&s_animation_cache, path, (void **)&decoder, out_file_size)) {
        return NULL;
    }

    if (ui_dual_gif_decoder_is_cfa(decoder)) {
        ui_dual_gif_subtract_cfa_cache_bytes(animation_decoder_file_size(decoder));
    }

    (void)animation_decoder_rewind(decoder);
    return decoder;
}

static bool ui_dual_gif_has_cached_animation(const char *path)
{
    /* A successful lookup means the animation is about to be used or relied on
     * as a future transition, so refresh its LRU stamp as well. */
    return path && media_cache_get(&s_animation_cache, path, NULL, NULL);
}

static void ui_dual_gif_store_cached_animation(const char *path, animation_decoder_t *gif, size_t file_size)
{
    if (!path || !gif) {
        return;
    }

    if (ui_dual_gif_decoder_is_cfa(gif)) {
        if (!animation_decoder_is_preloaded(gif)) {
            ESP_LOGI(TAG, "skip cfa cache path=%s size=%u reason=streaming_decoder", path, (unsigned)file_size);
            animation_decoder_close(gif);
            return;
        }
        if (!ui_dual_gif_cfa_cache_can_store(file_size)) {
            ESP_LOGI(TAG,
                     "skip cfa cache path=%s size=%u cached=%u free_psram=%u cap=%u margin=%u",
                     path,
                     (unsigned)file_size,
                     (unsigned)s_animation_cfa_cache_bytes,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES,
                     (unsigned)CONFIG_FAIRYTALE_ANIM_CFA_FREE_PSRAM_MARGIN_BYTES);
            animation_decoder_close(gif);
            return;
        }
    }

    (void)animation_decoder_rewind(gif);
    if (!media_cache_put(&s_animation_cache, path, gif, file_size)) {
        animation_decoder_close(gif);
        return;
    }

    if (ui_dual_gif_decoder_is_cfa(gif)) {
        s_animation_cfa_cache_bytes += file_size;
    }
}

static void ui_dual_gif_purge_cached_book_cfa_path(const char *path, const char *keep_path)
{
    animation_decoder_t *decoder = NULL;
    size_t file_size = 0U;

    if (!ui_dual_gif_path_is_cfa(path)) {
        return;
    }
    if (keep_path && ui_dual_gif_paths_match(path, keep_path)) {
        return;
    }

    decoder = ui_dual_gif_take_cached_animation(path, &file_size);
    if (!decoder) {
        return;
    }

    ESP_LOGI(TAG, "purged cached book cfa path=%s size=%u", path, (unsigned)file_size);
    animation_decoder_close(decoder);
}

static void ui_dual_gif_purge_book_cfa_cache_except(const char *keep_path)
{
    ui_dual_gif_purge_cached_book_cfa_path(UI_DUAL_GIF_OPEN_PATH, keep_path);
    ui_dual_gif_purge_cached_book_cfa_path(UI_DUAL_GIF_SWIPE_PATH, keep_path);
    ui_dual_gif_purge_cached_book_cfa_path(UI_DUAL_GIF_CLOSE_PATH, keep_path);
}

static SemaphoreHandle_t ui_dual_gif_ensure_done_sem(SemaphoreHandle_t *slot)
{
    if (!slot) {
        return NULL;
    }

    if (!*slot) {
        *slot = xSemaphoreCreateBinary();
    }

    return *slot;
}

static void ui_dual_gif_drain_done_sem(SemaphoreHandle_t sem)
{
    if (!sem) {
        return;
    }

    while (xSemaphoreTake(sem, 0) == pdTRUE) {
    }
}

static void ui_dual_gif_wait_for_signal(const char *tag,
                                        const char *path,
                                        volatile TaskHandle_t *task_slot,
                                        SemaphoreHandle_t sem,
                                        uint32_t timeout_ms)
{
    TickType_t elapsed = 0;
    bool timeout_logged = false;

    while (task_slot && *task_slot) {
        TickType_t wait_ticks = pdMS_TO_TICKS(UI_DUAL_GIF_STOP_WAIT_SLICE_MS);

        if (sem) {
            (void)xSemaphoreTake(sem, wait_ticks);
        } else {
            vTaskDelay(wait_ticks);
        }

        if (!timeout_logged && timeout_ms > 0U) {
            elapsed += wait_ticks;
            if (elapsed >= pdMS_TO_TICKS(timeout_ms) && task_slot && *task_slot) {
                ESP_LOGW(TAG, "%s for %s exceeded %u ms, waiting for cooperative stop",
                         tag,
                         path ? path : "(null)",
                         (unsigned)timeout_ms);
                timeout_logged = true;
            }
        }
    }
}

static void ui_dual_gif_clear_preloaded_file_internal(ui_dual_gif_player_t *player,
                                                      bool allow_cache,
                                                      const char *keep_path)
{
    const char *path = NULL;
    size_t file_size = 0U;

    if (!player) {
        return;
    }

    path = player->preloaded_path;
    file_size = player->preloaded_file_size;
    if (player->preloaded_gif) {
        if (keep_path && ui_dual_gif_paths_match(path, keep_path)) {
            ESP_LOGI(TAG,
                     "keep preloaded book cfa path=%s size=%u reason=next_clip",
                     path ? path : "(null)",
                     (unsigned)file_size);
            return;
        } else if (allow_cache) {
            ui_dual_gif_store_cached_animation(path, player->preloaded_gif, file_size);
        } else {
            ESP_LOGI(TAG,
                     "discard preloaded book cfa path=%s size=%u reason=book_cfa_memory_reclaim",
                     path ? path : "(null)",
                     (unsigned)file_size);
            animation_decoder_close(player->preloaded_gif);
        }
    }

    player->preloaded_gif = NULL;

    player->preloaded_file_size = 0;
    player->preloaded_path = NULL;
    player->preloaded_sequence_index = 0;
}

static void ui_dual_gif_clear_preloaded_file(ui_dual_gif_player_t *player)
{
    ui_dual_gif_clear_preloaded_file_internal(player, true, NULL);
}

static void ui_dual_gif_cancel_preload(ui_dual_gif_player_t *player, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (!player) {
        return;
    }

    player->preload_target_path = NULL;
    player->preload_target_index = 0U;
    player->preload_enqueued = false;
    player->preload_generation++;
}

static bool ui_dual_gif_preload_job_stale(void *ctx, uint32_t generation)
{
    ui_dual_gif_preload_job_t *job = (ui_dual_gif_preload_job_t *)ctx;
    ui_dual_gif_player_t *player = job ? job->player : NULL;

    return !player ||
           generation != player->preload_generation ||
           !job ||
           !ui_dual_gif_paths_match(player->preload_target_path, job->path) ||
           player->preload_target_index != job->sequence_index;
}

static bool ui_dual_gif_book_cfa_preload_should_wait(const ui_dual_gif_player_t *player, const char *path)
{
    const bool audio_active = ui_dual_gif_book_preload_audio_active();
    const bool urgent_close = path && ui_dual_gif_paths_match(path, UI_DUAL_GIF_CLOSE_PATH);

    return player == &s_book_player &&
           path &&
           ui_dual_gif_path_is_cfa(path) &&
           !urgent_close &&
           audio_active;
}

static bool ui_dual_gif_book_preload_audio_active(void)
{
    audio_session_metrics_t metrics = {0};
    const bool ui_audio_active = s_audio_active;
    bool session_audio_active = false;

    audio_session_get_metrics(&metrics);
    session_audio_active = metrics.active || metrics.active_session_id != 0U;

    if (ui_audio_active != session_audio_active) {
        ESP_LOGI(TAG,
                 "book preload audio mismatch ui_active=%u session_active=%u session_id=%u paused=%u",
                 ui_audio_active ? 1U : 0U,
                 session_audio_active ? 1U : 0U,
                 (unsigned)metrics.active_session_id,
                 metrics.paused ? 1U : 0U);
    }

    return ui_audio_active || session_audio_active;
}

static bool ui_dual_gif_preload_job_run(void *ctx, uint32_t generation)
{
    ui_dual_gif_preload_job_t *job = (ui_dual_gif_preload_job_t *)ctx;
    ui_dual_gif_player_t *player = job ? job->player : NULL;
    animation_decoder_t *preloaded_gif = NULL;
    size_t file_size = 0U;
    int frame_result = 0;

    if (!job || !player || generation != job->generation || !job->path) {
        return false;
    }

    if (ui_dual_gif_book_cfa_preload_should_wait(player, job->path)) {
        player->preload_target_path = NULL;
        player->preload_target_index = 0U;
        player->preload_enqueued = false;

        ESP_LOGI(TAG,
                 "defer book preload path=%s reason=audio_active_loader audio_active=%u",
                 job->path,
                 s_audio_active ? 1U : 0U);
        return ui_dual_gif_defer_book_preload_until_audio_stable(job->path);
    }

    preloaded_gif = ui_dual_gif_take_cached_animation(job->path, &file_size);
    if (!preloaded_gif &&
        animation_decoder_open(job->path, ANIMATION_DECODER_KIND_AUTO, &preloaded_gif) == ESP_OK) {
        file_size = animation_decoder_file_size(preloaded_gif);
    }

    if (preloaded_gif) {
        frame_result = animation_decoder_frame_ready(preloaded_gif) ? 1 : animation_decoder_decode_next(preloaded_gif);
    }

    if (preloaded_gif && ui_dual_gif_decoder_is_cfa(preloaded_gif) &&
        !ui_dual_gif_cfa_decoder_is_preloaded(preloaded_gif)) {
        ESP_LOGI(TAG,
                 "discard cfa preload path=%s size=%u reason=streaming_decoder",
                 job->path,
                 (unsigned)file_size);
        animation_decoder_close(preloaded_gif);
        preloaded_gif = NULL;
        frame_result = 0;
    }

    if (preloaded_gif && frame_result > 0 && !ui_dual_gif_preload_job_stale(job, generation)) {
        ui_dual_gif_clear_preloaded_file(player);
        player->preloaded_gif = preloaded_gif;
        player->preloaded_file_size = file_size;
        player->preloaded_path = job->path;
        player->preloaded_sequence_index = job->sequence_index;
        player->preload_enqueued = false;
        preloaded_gif = NULL;

        UI_DIAG_LOGI("preloaded gif seq %u/%u: %s size=%u",
                     (unsigned)(job->sequence_index + 1U),
                     (unsigned)player->sequence_count,
                     job->path,
                     (unsigned)player->preloaded_file_size);
    }

    if (preloaded_gif) {
        if (ui_dual_gif_preload_job_stale(job, generation)) {
            ESP_LOGI(TAG,
                     "discard stale preload path=%s size=%u",
                     job->path,
                     (unsigned)file_size);
            animation_decoder_close(preloaded_gif);
        } else {
            ui_dual_gif_store_cached_animation(job->path, preloaded_gif, file_size);
        }
    }

    if (!ui_dual_gif_preload_job_stale(job, generation)) {
        player->preload_target_path = NULL;
        player->preload_target_index = 0U;
        player->preload_enqueued = false;
    }

    return false;
}

static void ui_dual_gif_preload_job_free(void *ctx)
{
    free(ctx);
}

static bool ui_dual_gif_preload_path(ui_dual_gif_player_t *player, const char *path)
{
    ui_dual_gif_preload_job_t *job = NULL;
    media_loader_request_t request = {0};

    if (!player || !path) {
        return false;
    }

    if (ui_dual_gif_book_cfa_preload_should_wait(player, path)) {
        return ui_dual_gif_defer_book_preload_until_audio_stable(path);
    }

    if (player == &s_book_player &&
        player->gif &&
        player->path &&
        ui_dual_gif_paths_match(player->path, path)) {
        return true;
    }

    if (player->preloaded_gif &&
        player->preloaded_path &&
        ui_dual_gif_paths_match(player->preloaded_path, path)) {
        return true;
    }

    if (ui_dual_gif_has_cached_animation(path)) {
        return true;
    }

    if (player->preload_enqueued &&
        player->preload_target_path &&
        ui_dual_gif_paths_match(player->preload_target_path, path) &&
        player->preload_target_index == 0U) {
        return true;
    }

    ui_dual_gif_cancel_preload(player, UI_DUAL_GIF_PRELOAD_STOP_WAIT_MS);
    ui_dual_gif_clear_preloaded_file(player);

    job = malloc(sizeof(*job));
    if (!job) {
        ESP_LOGW(TAG, "preload job allocation failed for %s", path);
        return false;
    }

    job->player = player;
    job->path = path;
    job->sequence_index = 0U;
    job->generation = ++player->preload_generation;

    player->preload_target_path = path;
    player->preload_target_index = 0U;
    player->preload_enqueued = true;

    request.debug_tag = "anim_preload";
    request.ctx = job;
    request.generation = job->generation;
    request.priority = player == &s_book_player ?
                       MEDIA_LOADER_PRIORITY_NEXT_TRANSITION :
                       MEDIA_LOADER_PRIORITY_IDLE_PREPARE;
    request.run_cb = ui_dual_gif_preload_job_run;
    request.stale_cb = ui_dual_gif_preload_job_stale;
    request.free_cb = ui_dual_gif_preload_job_free;

    if (media_loader_service_enqueue(&request) != ESP_OK) {
        ESP_LOGW(TAG, "unable to enqueue preload job for %s", path);
        player->preload_target_path = NULL;
        player->preload_target_index = 0U;
        player->preload_enqueued = false;
        ui_dual_gif_preload_job_free(job);
        return false;
    }

    return true;
}

static void ui_dual_gif_cancel_deferred_book_preload(void)
{
    if (s_book_preload_defer_timer) {
        lv_timer_del(s_book_preload_defer_timer);
        s_book_preload_defer_timer = NULL;
    }
    s_deferred_book_preload_path = NULL;
}

static void ui_dual_gif_deferred_book_preload_timer_cb(lv_timer_t *timer)
{
    const char *path = s_deferred_book_preload_path;
    const bool audio_active = ui_dual_gif_book_preload_audio_active();

    if (s_book_preload_defer_timer == timer) {
        s_book_preload_defer_timer = NULL;
    }
    s_deferred_book_preload_path = NULL;

    if (!path) {
        return;
    }

    ESP_LOGI(TAG,
             "deferred book preload start path=%s audio_active=%u delay_ms=%u",
             path,
             audio_active ? 1U : 0U,
             (unsigned)UI_DUAL_GIF_BOOK_PRELOAD_AUDIO_STABLE_DELAY_MS);
    (void)ui_dual_gif_preload_path(&s_book_player, path);
}

static void ui_dual_gif_arm_deferred_book_preload_timer(void)
{
    const bool audio_active = ui_dual_gif_book_preload_audio_active();
    const uint32_t delay_ms = audio_active ? UI_DUAL_GIF_BOOK_PRELOAD_AUDIO_STABLE_DELAY_MS : 1U;
    const char *reason = audio_active ? "audio_stabilize" : "audio_idle";

    if (!s_deferred_book_preload_path) {
        return;
    }

    if (!s_book_preload_defer_timer) {
        s_book_preload_defer_timer = lv_timer_create(ui_dual_gif_deferred_book_preload_timer_cb,
                                                     delay_ms,
                                                     NULL);
        if (!s_book_preload_defer_timer) {
            ESP_LOGW(TAG,
                     "unable to create deferred book preload timer for %s",
                     s_deferred_book_preload_path);
            ui_dual_gif_deferred_book_preload_timer_cb(NULL);
            return;
        }
        lv_timer_set_repeat_count(s_book_preload_defer_timer, 1);
    } else {
        lv_timer_set_period(s_book_preload_defer_timer, delay_ms);
        lv_timer_reset(s_book_preload_defer_timer);
        lv_timer_resume(s_book_preload_defer_timer);
    }

    ESP_LOGI(TAG,
             "defer book preload path=%s delay_ms=%u reason=%s",
             s_deferred_book_preload_path,
             (unsigned)delay_ms,
             reason);
}

static bool ui_dual_gif_preload_book_path_now(const char *path)
{
    ui_dual_gif_cancel_deferred_book_preload();
    return ui_dual_gif_preload_path(&s_book_player, path);
}

static bool ui_dual_gif_defer_book_preload_until_audio_stable(const char *path)
{
    const bool audio_active = ui_dual_gif_book_preload_audio_active();

    if (!path) {
        return false;
    }

    if (s_book_player.preloaded_gif &&
        s_book_player.preloaded_path &&
        ui_dual_gif_paths_match(s_book_player.preloaded_path, path)) {
        return true;
    }

    if (ui_dual_gif_has_cached_animation(path)) {
        return true;
    }

    if (!audio_active) {
        ESP_LOGI(TAG,
                 "start book preload path=%s reason=audio_idle",
                 path);
        return ui_dual_gif_preload_book_path_now(path);
    }

    s_deferred_book_preload_path = path;
    ESP_LOGI(TAG,
             "defer book preload path=%s reason=wait_for_audio_start audio_active=%u",
             path,
             audio_active ? 1U : 0U);

    return true;
}

static bool ui_dual_gif_area_is_valid(const lv_area_t *area)
{
    return area && area->x1 <= area->x2 && area->y1 <= area->y2;
}

static lv_area_t ui_dual_gif_current_frame_area(const ui_dual_gif_player_t *player)
{
    animation_decoder_frame_view_t view = {0};
    lv_area_t area = {0};

    if (!player || !player->gif || animation_decoder_frame_view(player->gif, &view) != ESP_OK ||
        view.frame_width == 0U || view.frame_height == 0U) {
        return area;
    }

    area.x1 = (lv_coord_t)view.frame_x;
    area.y1 = (lv_coord_t)view.frame_y;
    area.x2 = (lv_coord_t)(view.frame_x + view.frame_width) - 1;
    area.y2 = (lv_coord_t)(view.frame_y + view.frame_height) - 1;
    return area;
}

static lv_area_t ui_dual_gif_union_area(lv_area_t a, lv_area_t b)
{
    if (!ui_dual_gif_area_is_valid(&a)) {
        return b;
    }
    if (!ui_dual_gif_area_is_valid(&b)) {
        return a;
    }

    if (b.x1 < a.x1) {
        a.x1 = b.x1;
    }
    if (b.y1 < a.y1) {
        a.y1 = b.y1;
    }
    if (b.x2 > a.x2) {
        a.x2 = b.x2;
    }
    if (b.y2 > a.y2) {
        a.y2 = b.y2;
    }

    return a;
}

static uint32_t ui_dual_gif_area_width(const lv_area_t *area)
{
    if (!ui_dual_gif_area_is_valid(area)) {
        return 0;
    }

    return (uint32_t)(area->x2 - area->x1 + 1);
}

static uint32_t ui_dual_gif_area_height(const lv_area_t *area)
{
    if (!ui_dual_gif_area_is_valid(area)) {
        return 0;
    }

    return (uint32_t)(area->y2 - area->y1 + 1);
}

static uint32_t ui_dual_gif_area_px_count(const lv_area_t *area)
{
    return ui_dual_gif_area_width(area) * ui_dual_gif_area_height(area);
}

static bool ui_dual_gif_use_local_palette(const ui_dual_gif_player_t *player)
{
    animation_decoder_frame_view_t view = {0};

    return player && player->gif &&
           animation_decoder_frame_view(player->gif, &view) == ESP_OK &&
           view.local_palette;
}

static void ui_dual_gif_prepare_palette(ui_dual_gif_player_t *player)
{
    animation_decoder_frame_view_t view = {0};
    int i = 0;

    if (!player || !player->gif || animation_decoder_frame_view(player->gif, &view) != ESP_OK ||
        !view.palette_rgb888) {
        return;
    }

    if (view.palette_rgb888 == player->pal565_source) {
        return;
    }

    for (i = 0; i < view.palette_size && i < 256; ++i) {
        const uint8_t *color = &view.palette_rgb888[i * 3];
        player->pal565[i] = lv_color_make(color[0], color[1], color[2]);
    }
    player->pal565_source = view.palette_rgb888;
}

static bool ui_dual_gif_resize_restore_buffers(ui_dual_gif_player_t *player, uint32_t px_count)
{
    lv_color_t *new_color_buf = NULL;
    uint8_t *new_alpha_buf = NULL;

    if (!player || px_count == 0 || px_count <= player->restore_buf_px_capacity) {
        return px_count != 0;
    }

    new_color_buf = heap_caps_malloc((size_t)px_count * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!new_color_buf) {
        new_color_buf = heap_caps_malloc((size_t)px_count * sizeof(lv_color_t), MALLOC_CAP_8BIT);
    }
    if (!new_color_buf) {
        return false;
    }

    new_alpha_buf = heap_caps_malloc(px_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!new_alpha_buf) {
        new_alpha_buf = heap_caps_malloc(px_count, MALLOC_CAP_8BIT);
    }
    if (!new_alpha_buf) {
        heap_caps_free(new_color_buf);
        return false;
    }

    if (player->restore_color_buf) {
        heap_caps_free(player->restore_color_buf);
    }
    if (player->restore_alpha_buf) {
        heap_caps_free(player->restore_alpha_buf);
    }

    player->restore_color_buf = new_color_buf;
    player->restore_alpha_buf = new_alpha_buf;
    player->restore_buf_px_capacity = px_count;
    return true;
}

static bool ui_dual_gif_save_restore_snapshot(ui_dual_gif_player_t *player, const lv_area_t *area)
{
    animation_decoder_canvas_info_t canvas_info = {0};
    const uint32_t width = ui_dual_gif_area_width(area);
    const uint32_t height = ui_dual_gif_area_height(area);
    uint32_t row = 0;

    if (!player || !player->gif || !player->lv_buf || !player->alpha_buf || !ui_dual_gif_area_is_valid(area)) {
        return false;
    }

    if (!ui_dual_gif_resize_restore_buffers(player, ui_dual_gif_area_px_count(area))) {
        ESP_LOGW(TAG, "restore snapshot allocation failed for %s", player->path);
        player->restore_snapshot_valid = false;
        return false;
    }

    if (animation_decoder_canvas_info(player->gif, &canvas_info) != ESP_OK) {
        return false;
    }

    for (row = 0; row < height; ++row) {
        const uint32_t src_off = ((uint32_t)area->y1 + row) * canvas_info.width + (uint32_t)area->x1;
        const uint32_t dst_off = row * width;

        memcpy(player->restore_color_buf + dst_off, player->lv_buf + src_off, width * sizeof(lv_color_t));
        memcpy(player->restore_alpha_buf + dst_off, player->alpha_buf + src_off, width);
    }

    player->restore_area = *area;
    player->restore_snapshot_valid = true;
    return true;
}

static void ui_dual_gif_restore_snapshot(ui_dual_gif_player_t *player)
{
    animation_decoder_canvas_info_t canvas_info = {0};
    const uint32_t width = ui_dual_gif_area_width(&player->restore_area);
    const uint32_t height = ui_dual_gif_area_height(&player->restore_area);
    uint32_t row = 0;

    if (!player || !player->gif || !player->restore_snapshot_valid || !player->lv_buf || !player->alpha_buf) {
        return;
    }

    if (animation_decoder_canvas_info(player->gif, &canvas_info) != ESP_OK) {
        return;
    }

    for (row = 0; row < height; ++row) {
        const uint32_t dst_off = ((uint32_t)player->restore_area.y1 + row) * canvas_info.width +
                                 (uint32_t)player->restore_area.x1;
        const uint32_t src_off = row * width;

        memcpy(player->lv_buf + dst_off, player->restore_color_buf + src_off, width * sizeof(lv_color_t));
        memcpy(player->alpha_buf + dst_off, player->restore_alpha_buf + src_off, width);
    }

    player->restore_snapshot_valid = false;
}

static void ui_dual_gif_clear_area_transparent(ui_dual_gif_player_t *player, const lv_area_t *area)
{
    animation_decoder_canvas_info_t canvas_info = {0};
    const uint32_t width = ui_dual_gif_area_width(area);
    const uint32_t height = ui_dual_gif_area_height(area);
    uint32_t row = 0;

    if (!player || !player->gif || !player->lv_buf || !player->alpha_buf || !ui_dual_gif_area_is_valid(area)) {
        return;
    }

    if (animation_decoder_canvas_info(player->gif, &canvas_info) != ESP_OK) {
        return;
    }

    for (row = 0; row < height; ++row) {
        const uint32_t off = ((uint32_t)area->y1 + row) * canvas_info.width + (uint32_t)area->x1;

        memset(player->lv_buf + off, 0, width * sizeof(lv_color_t));
        memset(player->alpha_buf + off, LV_OPA_TRANSP, width);
    }
}

static void ui_dual_gif_overlay_frame_area(ui_dual_gif_player_t *player, const lv_area_t *area)
{
    animation_decoder_frame_view_t view = {0};
    uint32_t width = 0;
    lv_coord_t y = 0;

    if (!player || !player->gif || !player->lv_buf || !player->alpha_buf ||
        !ui_dual_gif_area_is_valid(area) || animation_decoder_frame_view(player->gif, &view) != ESP_OK) {
        return;
    }

    width = view.canvas_width;

    for (y = area->y1; y <= area->y2; ++y) {
        uint32_t pos = ((uint32_t)y * width) + (uint32_t)area->x1;
        lv_coord_t x = 0;

        for (x = area->x1; x <= area->x2; ++x, ++pos) {
            const uint8_t index = view.frame_indices[pos];

            if (view.transparency && index == view.transparent_index) {
                continue;
            } else {
                player->lv_buf[pos] = player->pal565[index];
                player->alpha_buf[pos] = LV_OPA_COVER;
            }
        }
    }
}

static void ui_dual_gif_pack_true_color_alpha(ui_dual_gif_player_t *player)
{
    uint32_t i = 0;
    uint8_t *dst = NULL;

    if (!player || !player->use_true_color_alpha || !player->img_buf || !player->lv_buf || !player->alpha_buf) {
        return;
    }

    dst = player->img_buf;
    for (i = 0; i < player->render_px_count; ++i) {
        memcpy(dst, &player->lv_buf[i], sizeof(lv_color_t));
        dst += sizeof(lv_color_t);
        *dst++ = player->alpha_buf[i];
    }
}

static void ui_dual_gif_sync_rgb565a8_area(ui_dual_gif_player_t *player, const lv_area_t *area)
{
    const uint32_t width = ui_dual_gif_area_width(area);
    const uint32_t height = ui_dual_gif_area_height(area);
    lv_color_t *img_lv_buf = NULL;
    uint8_t *img_alpha_buf = NULL;
    uint32_t row = 0;

    if (!player || player->use_true_color_alpha || !player->img_buf || !player->render_buf ||
        player->img_buf == player->render_buf || !player->lv_buf || !player->alpha_buf ||
        !ui_dual_gif_area_is_valid(area)) {
        return;
    }

    img_lv_buf = (lv_color_t *)player->img_buf;
    img_alpha_buf = player->img_buf + (player->render_px_count * sizeof(lv_color_t));

    for (row = 0; row < height; ++row) {
        const uint32_t off = ((uint32_t)area->y1 + row) * player->render_w + (uint32_t)area->x1;

        memcpy(img_lv_buf + off, player->lv_buf + off, width * sizeof(lv_color_t));
        memcpy(img_alpha_buf + off, player->alpha_buf + off, width);
    }
}

static void ui_dual_gif_set_obj_translate_x(void *obj, int32_t value)
{
    if (!obj) {
        return;
    }

    lv_obj_set_style_translate_x((lv_obj_t *)obj, (lv_coord_t)value, LV_PART_MAIN);
}

static void ui_dual_gif_set_obj_translate_y(void *obj, int32_t value)
{
    if (!obj) {
        return;
    }

    lv_obj_set_style_translate_y((lv_obj_t *)obj, (lv_coord_t)value, LV_PART_MAIN);
}

static void ui_dual_gif_set_obj_img_opa(void *obj, int32_t value)
{
    if (!obj) {
        return;
    }

    if (value < LV_OPA_TRANSP) {
        value = LV_OPA_TRANSP;
    } else if (value > LV_OPA_COVER) {
        value = LV_OPA_COVER;
    }

    lv_obj_set_style_img_opa((lv_obj_t *)obj, (lv_opa_t)value, LV_PART_MAIN);
}

static void ui_dual_gif_cancel_overlay_swipe_timers(void)
{
    if (s_overlay_swipe_hide_timer) {
        lv_timer_del(s_overlay_swipe_hide_timer);
        s_overlay_swipe_hide_timer = NULL;
    }

    if (s_overlay_swipe_show_timer) {
        lv_timer_del(s_overlay_swipe_show_timer);
        s_overlay_swipe_show_timer = NULL;
    }
}

static void ui_dual_gif_cancel_overlay_hide_timer(void)
{
    if (s_overlay_hide_timer) {
        lv_timer_del(s_overlay_hide_timer);
        s_overlay_hide_timer = NULL;
    }
}

static void ui_dual_gif_cancel_overlay_corner_anims(void)
{
    if (s_corner1_img) {
        lv_anim_del(s_corner1_img, ui_dual_gif_set_obj_img_opa);
    }
    if (s_corner2_img) {
        lv_anim_del(s_corner2_img, ui_dual_gif_set_obj_img_opa);
    }
    if (s_corner3_img) {
        lv_anim_del(s_corner3_img, ui_dual_gif_set_obj_img_opa);
    }
    if (s_corner4_img) {
        lv_anim_del(s_corner4_img, ui_dual_gif_set_obj_img_opa);
    }
}

static const lv_img_dsc_t *ui_dual_gif_get_default_overlay_image(void)
{
    return s_default_overlay_img;
}

static void ui_dual_gif_log_overlay_runtime_state(const char *stage, const ui_dual_gif_story_content_t *content)
{
    const void *actual_src = NULL;
    const lv_img_dsc_t *actual_dsc = NULL;
    const lv_img_dsc_t *staged_dsc = content ? content->overlay_image : s_staged_story_content.overlay_image;
    const bool layer_hidden = !s_book_overlay_layer || lv_obj_has_flag(s_book_overlay_layer, LV_OBJ_FLAG_HIDDEN);
    const bool img_hidden = !s_a_small_img || lv_obj_has_flag(s_a_small_img, LV_OBJ_FLAG_HIDDEN);
    const lv_opa_t layer_opa = s_book_overlay_layer ? lv_obj_get_style_opa(s_book_overlay_layer, LV_PART_MAIN) : LV_OPA_TRANSP;
    const lv_opa_t img_opa = s_a_small_img ? lv_obj_get_style_img_opa(s_a_small_img, LV_PART_MAIN) : LV_OPA_TRANSP;

    if (s_a_small_img) {
        actual_src = lv_img_get_src(s_a_small_img);
        actual_dsc = (const lv_img_dsc_t *)actual_src;
    }

    UI_DIAG_LOGI("scene_runtime stage=%s case=%d node=%s uri=%s "
                 "staged_size=%u staged_w=%u staged_h=%u staged_cf=%u "
                 "actual_size=%u actual_w=%u actual_h=%u actual_cf=%u "
                 "layer_hidden=%u layer_opa=%u img_hidden=%u img_opa=%u",
                 stage ? stage : "<null>",
                 content ? content->debug_case_id : s_staged_story_content.debug_case_id,
                 content && content->debug_node_name ? content->debug_node_name :
                 (s_staged_story_content.debug_node_name ? s_staged_story_content.debug_node_name : "<null>"),
                 content && content->debug_image_uri ? content->debug_image_uri :
                 (s_staged_story_content.debug_image_uri ? s_staged_story_content.debug_image_uri : "<null>"),
                 staged_dsc ? (unsigned)staged_dsc->data_size : 0U,
                 staged_dsc ? (unsigned)staged_dsc->header.w : 0U,
                 staged_dsc ? (unsigned)staged_dsc->header.h : 0U,
                 staged_dsc ? (unsigned)staged_dsc->header.cf : 0U,
                 actual_dsc ? (unsigned)actual_dsc->data_size : 0U,
                 actual_dsc ? (unsigned)actual_dsc->header.w : 0U,
                 actual_dsc ? (unsigned)actual_dsc->header.h : 0U,
                 actual_dsc ? (unsigned)actual_dsc->header.cf : 0U,
                 layer_hidden ? 1U : 0U,
                 (unsigned)layer_opa,
                 img_hidden ? 1U : 0U,
                 (unsigned)img_opa);
}

static void ui_dual_gif_invalidate_overlay_src(const void *src, const char *stage)
{
    const lv_img_dsc_t *img = (const lv_img_dsc_t *)src;

    if (!src) {
        return;
    }

    lv_img_cache_invalidate_src(src);
    UI_DIAG_LOGI("scene_runtime cache_invalidate stage=%s size=%u",
                 stage ? stage : "<null>",
                 img ? (unsigned)img->data_size : 0U);
}

static void ui_dual_gif_force_overlay_redraw(const char *stage)
{
    if (s_a_small_img) {
        lv_obj_invalidate(s_a_small_img);
    }
    if (s_book_overlay_layer) {
        lv_obj_invalidate(s_book_overlay_layer);
    }
    UI_DIAG_LOGI("scene_runtime force_redraw stage=%s", stage ? stage : "<null>");
}

static void ui_dual_gif_release_a_small_bin(void)
{
    s_default_overlay_img = NULL;
}

static bool ui_dual_gif_load_decor_asset(ui_dual_gif_decor_asset_t *asset)
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

static const lv_img_dsc_t *ui_dual_gif_get_decor_asset(ui_dual_gif_decor_asset_id_t asset_id)
{
    ui_dual_gif_decor_asset_t *asset = NULL;

    if (asset_id < 0 || asset_id >= UI_DUAL_GIF_DECOR_ASSET_COUNT) {
        return NULL;
    }

    asset = &s_decor_assets[asset_id];
    if (!ui_dual_gif_load_decor_asset(asset)) {
        ESP_LOGW(TAG, "failed to load decor asset %s", asset->spiffs_path);
        return NULL;
    }

    return &asset->img_dsc;
}

static void ui_dual_gif_release_decor_assets(void)
{
    size_t i = 0;

    for (i = 0; i < UI_DUAL_GIF_ARRAY_SIZE(s_decor_assets); ++i) {
        /* LVGL caches variable images by the address of lv_img_dsc_t.  These
         * descriptors are static and reused when the story screen is created
         * again, so a cached decoder would otherwise retain the previous
         * (already freed) pixel pointer. */
        lv_img_cache_invalidate_src(&s_decor_assets[i].img_dsc);

        if (s_decor_assets[i].blob) {
            heap_caps_free(s_decor_assets[i].blob);
            s_decor_assets[i].blob = NULL;
        }

        memset(&s_decor_assets[i].img_dsc, 0, sizeof(s_decor_assets[i].img_dsc));
    }
}

static void ui_dual_gif_set_overlay_story_image(const lv_img_dsc_t *img_dsc)
{
    s_overlay_story_img = img_dsc ? img_dsc : ui_dual_gif_get_default_overlay_image();
}

static void ui_dual_gif_set_overlay_corners_opa(lv_opa_t value)
{
    if (s_corner1_img) {
        lv_obj_set_style_img_opa(s_corner1_img, value, LV_PART_MAIN);
    }
    if (s_corner2_img) {
        lv_obj_set_style_img_opa(s_corner2_img, value, LV_PART_MAIN);
    }
    if (s_corner3_img) {
        lv_obj_set_style_img_opa(s_corner3_img, value, LV_PART_MAIN);
    }
    if (s_corner4_img) {
        lv_obj_set_style_img_opa(s_corner4_img, value, LV_PART_MAIN);
    }
}

static lv_opa_t ui_dual_gif_get_overlay_corners_opa(void)
{
    if (s_corner1_img) {
        return lv_obj_get_style_img_opa(s_corner1_img, LV_PART_MAIN);
    }

    return LV_OPA_TRANSP;
}

static void ui_dual_gif_start_overlay_corners_fade(int32_t from, int32_t to, uint32_t duration_ms, uint32_t delay_ms)
{
    lv_anim_t anim;
    lv_obj_t *corner_imgs[] = {s_corner1_img, s_corner2_img, s_corner3_img, s_corner4_img};
    size_t i = 0;

    for (i = 0; i < sizeof(corner_imgs) / sizeof(corner_imgs[0]); ++i) {
        if (!corner_imgs[i]) {
            continue;
        }

        lv_anim_init(&anim);
        lv_anim_set_var(&anim, corner_imgs[i]);
        lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_img_opa);
        lv_anim_set_values(&anim, from, to);
        lv_anim_set_delay(&anim, (int32_t)delay_ms);
        lv_anim_set_time(&anim, (int32_t)duration_ms);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
    }
}

static void ui_dual_gif_reset_overlay_state(void)
{
    ui_dual_gif_cancel_overlay_swipe_timers();
    ui_dual_gif_cancel_overlay_hide_timer();
    ui_dual_gif_cancel_overlay_corner_anims();
    ui_dual_gif_cancel_story_text_timers();
    ui_dual_gif_cancel_story_text_anim();
    ui_dual_gif_cancel_story_choice_timers();
    ui_dual_gif_cancel_story_choice_anim();

    ui_dual_gif_set_overlay_story_image(NULL);
    s_show_choice_after_text_fade = false;
    s_scene_ready_pending = false;
    s_scene_overlay_ready = false;
    s_book_transition_ready = false;
    s_story_choice_waiting = false;
    ui_dual_gif_hide_story_text();
    ui_dual_gif_hide_story_choice();
    ui_dual_gif_hide_cloud();
    ui_dual_gif_finish_overlay_hide();
}

static void ui_dual_gif_layout_story_text(void)
{
    lv_coord_t label_y = UI_DUAL_GIF_STORY_TEXT_PAD_Y;
    lv_coord_t available_height = 0;
    lv_coord_t label_height = 0;
    lv_coord_t max_offset = 0;

    if (!s_story_text_view || !s_story_text_label) {
        return;
    }

    lv_obj_update_layout(s_story_text_view);

    available_height = UI_DUAL_GIF_STORY_TEXT_HEIGHT - (2 * UI_DUAL_GIF_STORY_TEXT_PAD_Y);
    label_height = lv_obj_get_height(s_story_text_label);

    if (label_height < available_height) {
        s_story_text_auto_scroll_offset = 0;
        label_y += (available_height - label_height) / 2;
    } else {
        max_offset = label_height - available_height;
        if (s_story_text_auto_scroll_offset > max_offset) {
            s_story_text_auto_scroll_offset = max_offset;
        }
        label_y -= s_story_text_auto_scroll_offset;
    }

    lv_obj_set_pos(s_story_text_label, UI_DUAL_GIF_STORY_TEXT_PAD_X, label_y);
}

static void ui_dual_gif_hide_story_text(void)
{
    if (!s_story_text_view) {
        return;
    }

    ui_dual_gif_cancel_story_text_auto_scroll();
    s_story_text_auto_scroll_offset = 0;
    lv_obj_scroll_to(s_story_text_view, 0, 0, LV_ANIM_OFF);
    lv_obj_set_style_opa(s_story_text_view, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_story_text_view, LV_OBJ_FLAG_HIDDEN);
    if (s_story_text_label) {
        lv_label_set_text(s_story_text_label, "");
        ui_dual_gif_layout_story_text();
    }
}

static void ui_dual_gif_clear_story_text_content(void)
{
    if (s_story_text_label) {
        lv_label_set_text(s_story_text_label, "");
        ui_dual_gif_layout_story_text();
    }

    if (s_story_choice_question_label) {
        lv_label_set_text(s_story_choice_question_label, "");
    }

    if (s_story_choice_button1_label) {
        lv_label_set_text(s_story_choice_button1_label, "");
    }

    if (s_story_choice_button2_label) {
        lv_label_set_text(s_story_choice_button2_label, "");
    }
}

static void ui_dual_gif_hide_story_choice(void)
{
    if (!s_story_choice_container) {
        return;
    }

    ui_dual_gif_schedule_story_choice_fade_out(UI_DUAL_GIF_STORY_CHOICE_FADE_OUT_DELAY_MS);
}

static void ui_dual_gif_show_story_choice(void)
{
    if (!s_story_choice_container) {
        return;
    }

    ui_dual_gif_schedule_story_choice_fade_in(UI_DUAL_GIF_STORY_CHOICE_FADE_IN_DELAY_MS);
}

static void ui_dual_gif_hide_story_img_button(void)
{
    if (!s_story_img_button) {
        return;
    }

    lv_obj_set_style_opa(s_story_img_button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_story_img_button, LV_OBJ_FLAG_HIDDEN);
}

static void ui_dual_gif_show_story_img_button(void)
{
    if (!s_story_img_button) {
        return;
    }

    lv_obj_clear_flag(s_story_img_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_story_img_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_move_foreground(s_story_img_button);
}

static int ui_dual_gif_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static void ui_dual_gif_refresh_settings_volume_icon(int volume)
{
    if (!s_settings_volume_icon) {
        return;
    }

    lv_img_set_src(s_settings_volume_icon,
                   volume <= UI_DUAL_GIF_SETTINGS_VOLUME_MIN ? &ui_img_vol_min_png : &ui_img_vol_png);
}

static void ui_dual_gif_refresh_settings_brightness_icon(int brightness)
{
    if (!s_settings_brightness_icon) {
        return;
    }

    lv_img_set_src(s_settings_brightness_icon,
                   brightness <= UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MIN ? &ui_img_bright_min_png : &ui_img_bright_png);
}

static void ui_dual_gif_refresh_settings_user_name(void)
{
    char user_name[SETTINGS_STORE_USER_NAME_MAX_LEN + 1] = {0};
    bool found = false;
    const char *display_name = UI_DUAL_GIF_SETTINGS_NAME_EMPTY;

    if (!s_settings_user_name_value_label) {
        return;
    }

    if (settings_store_get_user_name(user_name, sizeof(user_name), &found) == ESP_OK &&
        found &&
        user_name[0] != '\0') {
        display_name = user_name;
    }

    lv_label_set_text(s_settings_user_name_value_label, display_name);
}

static void ui_dual_gif_refresh_settings_controls(void)
{
    int volume = 0;
    int brightness = 0;
    int current_brightness = 0;

    if (s_settings_volume_slider) {
        volume = ui_dual_gif_clamp_int(audio_session_get_volume(),
                                       UI_DUAL_GIF_SETTINGS_VOLUME_MIN,
                                       UI_DUAL_GIF_SETTINGS_VOLUME_MAX);
        lv_slider_set_value(s_settings_volume_slider, volume, LV_ANIM_OFF);
        ui_dual_gif_refresh_settings_volume_icon(volume);
    }

    if (s_settings_brightness_slider) {
        current_brightness = bsp_display_brightness_get();
        brightness = ui_dual_gif_clamp_int(current_brightness,
                                           UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MIN,
                                           UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MAX);
        if (current_brightness != brightness) {
            (void)bsp_display_brightness_set(brightness);
        }
        lv_slider_set_value(s_settings_brightness_slider, brightness, LV_ANIM_OFF);
        ui_dual_gif_refresh_settings_brightness_icon(brightness);
    }

    ui_dual_gif_refresh_settings_user_name();
}

static void ui_dual_gif_style_settings_slider(lv_obj_t *slider)
{
    if (!slider) {
        return;
    }

    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFAC0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFAF190), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(slider, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFAF190), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, 255, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(slider, 8, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(slider, 8, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(slider, 8, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(slider, 8, LV_PART_KNOB | LV_STATE_DEFAULT);
}

static void ui_dual_gif_style_yellow_button_pressed_shadow(lv_obj_t *button)
{
    if (!button) {
        return;
    }

    lv_obj_set_style_shadow_color(button, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 30, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(button, 2, LV_PART_MAIN | LV_STATE_PRESSED);
}

static void ui_dual_gif_style_light_button_pressed_shadow(lv_obj_t *button)
{
    if (!button) {
        return;
    }

    lv_obj_set_style_shadow_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0xECCA5A), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 30, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(button, 2, LV_PART_MAIN | LV_STATE_PRESSED);
}

static void ui_dual_gif_settings_persist_volume(int volume)
{
    esp_err_t err = settings_store_set_volume(volume);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "unable to persist volume %d: %s", volume, esp_err_to_name(err));
    }
}

static void ui_dual_gif_settings_persist_brightness(int brightness)
{
    esp_err_t err = settings_store_set_display_brightness(brightness);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "unable to persist brightness %d: %s", brightness, esp_err_to_name(err));
    }
}

static void ui_dual_gif_hide_settings_toast(void)
{
    if (s_settings_toast_timer) {
        lv_timer_del(s_settings_toast_timer);
        s_settings_toast_timer = NULL;
    }

    if (s_settings_toast) {
        lv_anim_del(s_settings_toast, ui_dual_gif_set_obj_opa);
    }

    if (s_settings_toast_label) {
        lv_anim_del(s_settings_toast_label, ui_dual_gif_set_obj_opa);
        s_settings_toast_label = NULL;
    }

    if (s_settings_toast) {
        lv_obj_del(s_settings_toast);
        s_settings_toast = NULL;
    }
}

static void ui_dual_gif_settings_toast_fade_out_ready_cb(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    ui_dual_gif_hide_settings_toast();
}

static void ui_dual_gif_settings_toast_timer_cb(lv_timer_t *timer)
{
    lv_anim_t anim;

    if (s_settings_toast_timer == timer) {
        s_settings_toast_timer = NULL;
    }

    if (!s_settings_toast) {
        return;
    }

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_settings_toast);
    lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&anim, UI_DUAL_GIF_SETTINGS_TOAST_FADE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&anim, ui_dual_gif_settings_toast_fade_out_ready_cb);
    lv_anim_start(&anim);

    if (s_settings_toast_label) {
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, s_settings_toast_label);
        lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
        lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&anim, UI_DUAL_GIF_SETTINGS_TOAST_FADE_MS);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
        lv_anim_start(&anim);
    }
}

static void ui_dual_gif_show_settings_toast(const char *text)
{
    lv_obj_t *panel = NULL;
    lv_obj_t *label = NULL;
    lv_anim_t anim;

    if (!s_settings_screen || !text) {
        return;
    }

    ui_dual_gif_hide_settings_toast();

    panel = lv_obj_create(s_settings_screen);
    s_settings_toast = panel;
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 420, 88);
    lv_obj_set_align(panel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(panel, 40);
    lv_obj_set_style_radius(panel, 28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x262626), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_foreground(panel);

    label = lv_label_create(panel);
    s_settings_toast_label = label;
    lv_obj_set_width(label, 360);
    lv_obj_center(label);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFBF4D7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &ui_font_Font52, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_opa(label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, panel);
    lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&anim, UI_DUAL_GIF_SETTINGS_TOAST_FADE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, label);
    lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&anim, UI_DUAL_GIF_SETTINGS_TOAST_FADE_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);

    s_settings_toast_timer = lv_timer_create(ui_dual_gif_settings_toast_timer_cb,
                                             UI_DUAL_GIF_SETTINGS_TOAST_DURATION_MS,
                                             NULL);
    if (s_settings_toast_timer) {
        lv_timer_set_repeat_count(s_settings_toast_timer, 1);
    }
}

static void ui_dual_gif_dismiss_settings_confirm(void)
{
    if (s_settings_confirm_overlay) {
        lv_obj_del(s_settings_confirm_overlay);
        s_settings_confirm_overlay = NULL;
    }

    s_settings_confirm_action = UI_DUAL_GIF_SETTINGS_CONFIRM_NONE;
}

static void ui_dual_gif_settings_confirm_cancel_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    ui_dual_gif_dismiss_settings_confirm();
}

static void ui_dual_gif_settings_confirm_apply_event_cb(lv_event_t *e)
{
    esp_err_t err = ESP_OK;
    char user_name[SETTINGS_STORE_USER_NAME_MAX_LEN + 1] = {0};
    bool found = false;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    switch (s_settings_confirm_action) {
    case UI_DUAL_GIF_SETTINGS_CONFIRM_RESTART:
        ui_request_restart_to_title();
        (void)story_audio_play_cue(STORY_AUDIO_CUE_ACTION_CONFIRM);
        break;

    case UI_DUAL_GIF_SETTINGS_CONFIRM_CLEAR_NAME:
        ui_dual_gif_dismiss_settings_confirm();
        if (settings_store_get_user_name(user_name, sizeof(user_name), &found) != ESP_OK ||
            !found ||
            user_name[0] == '\0') {
            user_name_reset_cache();
            ui_dual_gif_refresh_settings_user_name();
            ui_dual_gif_show_settings_toast("Name already cleared");
            break;
        }

        err = settings_store_set_user_name("");
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unable to clear user name: %s", esp_err_to_name(err));
            return;
        }

        user_name_reset_cache();
        ui_dual_gif_refresh_settings_user_name();
        ui_dual_gif_show_settings_toast("Name cleared successfully");
        (void)story_audio_play_cue(STORY_AUDIO_CUE_ACTION_CONFIRM);
        break;

    case UI_DUAL_GIF_SETTINGS_CONFIRM_NONE:
    default:
        ui_dual_gif_dismiss_settings_confirm();
        break;
    }
}

static void ui_dual_gif_show_settings_confirm(const char *title,
                                              const char *body,
                                              const char *confirm_text,
                                              ui_dual_gif_settings_confirm_action_t action)
{
    lv_obj_t *panel = NULL;
    lv_obj_t *label = NULL;
    lv_obj_t *cancel_button = NULL;
    lv_obj_t *confirm_button = NULL;

    if (!s_settings_screen || !title || !body || !confirm_text) {
        return;
    }

    ui_dual_gif_dismiss_settings_confirm();
    s_settings_confirm_action = action;

    s_settings_confirm_overlay = lv_obj_create(s_settings_screen);
    lv_obj_remove_style_all(s_settings_confirm_overlay);
    lv_obj_set_size(s_settings_confirm_overlay, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_align(s_settings_confirm_overlay, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_settings_confirm_overlay, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_settings_confirm_overlay, LV_OPA_60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_settings_confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_settings_confirm_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_foreground(s_settings_confirm_overlay);

    panel = lv_obj_create(s_settings_confirm_overlay);
    lv_obj_set_size(panel, 560, 310);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x262626), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(panel);
    lv_obj_set_width(label, 460);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 34);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(0xECCA5A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &ui_font_Font6, LV_PART_MAIN | LV_STATE_DEFAULT);

    label = lv_label_create(panel);
    lv_obj_set_width(label, 440);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 112);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, body);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFBF4D7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &ui_font_Font52, LV_PART_MAIN | LV_STATE_DEFAULT);

    cancel_button = lv_btn_create(panel);
    lv_obj_set_size(cancel_button, 166, 62);
    lv_obj_align(cancel_button, LV_ALIGN_BOTTOM_LEFT, 72, -34);
    lv_obj_clear_flag(cancel_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(cancel_button, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cancel_button, lv_color_hex(0xFBF4D7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cancel_button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_dual_gif_style_light_button_pressed_shadow(cancel_button);
    lv_obj_add_event_cb(cancel_button, ui_dual_gif_settings_confirm_cancel_event_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(cancel_button);
    lv_obj_center(label);
    lv_label_set_text(label, "Cancel");
    lv_obj_set_style_text_color(label, lv_color_hex(0x171717), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &ui_font_Font51, LV_PART_MAIN | LV_STATE_DEFAULT);

    confirm_button = lv_btn_create(panel);
    lv_obj_set_size(confirm_button, 166, 62);
    lv_obj_align(confirm_button, LV_ALIGN_BOTTOM_RIGHT, -72, -34);
    lv_obj_clear_flag(confirm_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(confirm_button, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(confirm_button, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(confirm_button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(confirm_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_dual_gif_style_yellow_button_pressed_shadow(confirm_button);
    lv_obj_add_event_cb(confirm_button, ui_dual_gif_settings_confirm_apply_event_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(confirm_button);
    lv_obj_center(label);
    lv_label_set_text(label, confirm_text);
    lv_obj_set_style_text_color(label, lv_color_hex(0x171717), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &ui_font_Font51, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void ui_dual_gif_show_settings_screen(void)
{
    if (!s_settings_screen) {
        return;
    }

    ui_dual_gif_refresh_settings_controls();
    lv_obj_clear_flag(s_settings_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_settings_screen);
}

static void ui_dual_gif_hide_settings_screen(void)
{
    if (!s_settings_screen) {
        return;
    }

    ui_dual_gif_dismiss_settings_confirm();
    ui_dual_gif_hide_settings_toast();
    lv_obj_add_flag(s_settings_screen, LV_OBJ_FLAG_HIDDEN);
}

static void ui_dual_gif_settings_open_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (!s_settings_screen || !lv_obj_has_flag(s_settings_screen, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    if (s_callbacks.settings_opened) {
        s_callbacks.settings_opened(s_callbacks.user_ctx);
    }
    ui_dual_gif_show_settings_screen();
}

static void ui_dual_gif_settings_close_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (!s_settings_screen || lv_obj_has_flag(s_settings_screen, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    ui_dual_gif_hide_settings_screen();
    if (s_callbacks.settings_closed) {
        s_callbacks.settings_closed(s_callbacks.user_ctx);
    }
}

static void ui_dual_gif_settings_restart_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    ui_dual_gif_show_settings_confirm("Restart app?",
                                      "The application will reopen from the welcome screen.",
                                      "Restart",
                                      UI_DUAL_GIF_SETTINGS_CONFIRM_RESTART);
}

static void ui_dual_gif_settings_clear_name_event_cb(lv_event_t *e)
{
    char user_name[SETTINGS_STORE_USER_NAME_MAX_LEN + 1] = {0};
    bool found = false;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (settings_store_get_user_name(user_name, sizeof(user_name), &found) != ESP_OK ||
        !found ||
        user_name[0] == '\0') {
        user_name_reset_cache();
        ui_dual_gif_refresh_settings_user_name();
        ui_dual_gif_show_settings_toast("Name already cleared");
        (void)story_audio_play_cue(STORY_AUDIO_CUE_ACTION_CANCEL);
        return;
    }

    ui_dual_gif_show_settings_confirm("Clear user name?",
                                      "The saved name will be removed.",
                                      "Clear",
                                      UI_DUAL_GIF_SETTINGS_CONFIRM_CLEAR_NAME);
}

static void ui_dual_gif_settings_volume_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int requested = 0;
    int actual = 0;

    if (!slider) {
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui_dual_gif_settings_persist_volume(ui_dual_gif_clamp_int(audio_session_get_volume(),
                                                                  UI_DUAL_GIF_SETTINGS_VOLUME_MIN,
                                                                  UI_DUAL_GIF_SETTINGS_VOLUME_MAX));
        (void)story_audio_play_cue(STORY_AUDIO_CUE_VOLUME_RELEASE);
        return;
    }

    if (code != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    requested = ui_dual_gif_clamp_int(lv_slider_get_value(slider),
                                      UI_DUAL_GIF_SETTINGS_VOLUME_MIN,
                                      UI_DUAL_GIF_SETTINGS_VOLUME_MAX);
    actual = requested;
    if (audio_session_set_volume(requested, &actual) != ESP_OK) {
        actual = ui_dual_gif_clamp_int(audio_session_get_volume(),
                                       UI_DUAL_GIF_SETTINGS_VOLUME_MIN,
                                       UI_DUAL_GIF_SETTINGS_VOLUME_MAX);
        ESP_LOGW(TAG, "unable to set volume, keeping %d", actual);
    }

    actual = ui_dual_gif_clamp_int(actual,
                                   UI_DUAL_GIF_SETTINGS_VOLUME_MIN,
                                   UI_DUAL_GIF_SETTINGS_VOLUME_MAX);
    if (actual != requested) {
        lv_slider_set_value(slider, actual, LV_ANIM_OFF);
    }
    ui_dual_gif_refresh_settings_volume_icon(actual);
}

static void ui_dual_gif_settings_brightness_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int requested = 0;
    int actual = 0;

    if (!slider) {
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui_dual_gif_settings_persist_brightness(ui_dual_gif_clamp_int(bsp_display_brightness_get(),
                                                                      UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MIN,
                                                                      UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MAX));
        return;
    }

    if (code != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    requested = ui_dual_gif_clamp_int(lv_slider_get_value(slider),
                                      UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MIN,
                                      UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MAX);
    actual = requested;
    if (bsp_display_brightness_set(requested) != ESP_OK) {
        actual = ui_dual_gif_clamp_int(bsp_display_brightness_get(),
                                       UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MIN,
                                       UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MAX);
        ESP_LOGW(TAG, "unable to set brightness, keeping %d", actual);
    }

    if (actual != requested) {
        lv_slider_set_value(slider, actual, LV_ANIM_OFF);
    }
    ui_dual_gif_refresh_settings_brightness_icon(actual);
}

static void ui_dual_gif_set_obj_opa(void *obj, int32_t value)
{
    lv_obj_t *target = (lv_obj_t *)obj;

    if (!target) {
        return;
    }

    lv_obj_set_style_opa(target, (lv_opa_t)value, LV_PART_MAIN);
}

static void ui_dual_gif_cancel_story_text_timers(void)
{
    if (s_story_text_fade_in_timer) {
        lv_timer_del(s_story_text_fade_in_timer);
        s_story_text_fade_in_timer = NULL;
    }

    if (s_story_text_fade_out_timer) {
        lv_timer_del(s_story_text_fade_out_timer);
        s_story_text_fade_out_timer = NULL;
    }
}

static void ui_dual_gif_cancel_story_text_anim(void)
{
    if (s_story_text_view) {
        lv_anim_del(s_story_text_view, ui_dual_gif_set_obj_opa);
    }
}

static void ui_dual_gif_cancel_story_text_auto_scroll(void)
{
    if (s_story_text_auto_scroll_timer) {
        lv_timer_del(s_story_text_auto_scroll_timer);
        s_story_text_auto_scroll_timer = NULL;
    }
}

static uint32_t ui_dual_gif_story_text_auto_scroll_period_ms(void)
{
    if (s_staged_story_content.debug_case_id == 0) {
        return UI_DUAL_GIF_STORY_TEXT_AUTO_SCROLL_NODE_A_PERIOD_MS;
    }

    return UI_DUAL_GIF_STORY_TEXT_AUTO_SCROLL_PERIOD_MS;
}

static void ui_dual_gif_story_text_auto_scroll_timer_cb(lv_timer_t *timer)
{
    lv_coord_t available_height = 0;
    lv_coord_t label_height = 0;
    lv_coord_t max_offset = 0;
    lv_coord_t step = UI_DUAL_GIF_STORY_TEXT_AUTO_SCROLL_STEP_PX;

    LV_UNUSED(timer);

    if (!s_audio_active ||
        !s_story_text_view ||
        lv_obj_has_flag(s_story_text_view, LV_OBJ_FLAG_HIDDEN) ||
        !s_story_text_label ||
        lv_obj_has_flag(s_story_text_label, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    if (s_story_choice_container && !lv_obj_has_flag(s_story_choice_container, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_obj_update_layout(s_story_text_view);
    available_height = UI_DUAL_GIF_STORY_TEXT_HEIGHT - (2 * UI_DUAL_GIF_STORY_TEXT_PAD_Y);
    label_height = lv_obj_get_height(s_story_text_label);
    if (label_height <= available_height) {
        return;
    }

    max_offset = label_height - available_height;
    if (s_story_text_auto_scroll_offset >= max_offset) {
        return;
    }

    if ((s_story_text_auto_scroll_offset + step) > max_offset) {
        step = max_offset - s_story_text_auto_scroll_offset;
    }

    s_story_text_auto_scroll_offset += step;
    ui_dual_gif_layout_story_text();
    lv_obj_invalidate(s_story_text_view);
}

static void ui_dual_gif_maybe_start_story_text_auto_scroll(void)
{
    uint32_t period_ms = ui_dual_gif_story_text_auto_scroll_period_ms();

    if (!s_audio_active || !s_story_text_view) {
        ui_dual_gif_cancel_story_text_auto_scroll();
        return;
    }

    if (!s_story_text_auto_scroll_timer) {
        s_story_text_auto_scroll_timer = lv_timer_create(ui_dual_gif_story_text_auto_scroll_timer_cb,
                                                         period_ms,
                                                         NULL);
    }

    if (s_story_text_auto_scroll_timer) {
        lv_timer_set_repeat_count(s_story_text_auto_scroll_timer, -1);
        lv_timer_set_period(s_story_text_auto_scroll_timer, period_ms);
        lv_timer_reset(s_story_text_auto_scroll_timer);
        lv_timer_resume(s_story_text_auto_scroll_timer);
    }
}

static void ui_dual_gif_cancel_story_choice_timers(void)
{
    if (s_story_choice_fade_in_timer) {
        lv_timer_del(s_story_choice_fade_in_timer);
        s_story_choice_fade_in_timer = NULL;
    }

    if (s_story_choice_fade_out_timer) {
        lv_timer_del(s_story_choice_fade_out_timer);
        s_story_choice_fade_out_timer = NULL;
    }
}

static void ui_dual_gif_cancel_story_choice_anim(void)
{
    if (s_story_choice_container) {
        lv_anim_del(s_story_choice_container, ui_dual_gif_set_obj_opa);
    }
}

static void ui_dual_gif_story_text_fade_in_timer_cb(lv_timer_t *timer)
{
    if (s_story_text_fade_in_timer == timer) {
        s_story_text_fade_in_timer = NULL;
    }

    ui_dual_gif_start_story_text_fade_in_now();
}

static void ui_dual_gif_story_text_fade_out_ready_cb(lv_anim_t *anim)
{
    lv_obj_t *obj = anim ? (lv_obj_t *)anim->var : NULL;

    if (!obj) {
        return;
    }

    ui_dual_gif_cancel_story_text_auto_scroll();
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

    if (s_show_choice_after_text_fade) {
        s_show_choice_after_text_fade = false;
        ui_dual_gif_show_story_choice();
    }
}

static void ui_dual_gif_story_choice_fade_in_timer_cb(lv_timer_t *timer)
{
    if (s_story_choice_fade_in_timer == timer) {
        s_story_choice_fade_in_timer = NULL;
    }

    ui_dual_gif_start_story_choice_fade_in_now();
}

static void ui_dual_gif_story_choice_fade_out_ready_cb(lv_anim_t *anim)
{
    lv_obj_t *obj = anim ? (lv_obj_t *)anim->var : NULL;

    if (!obj) {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (s_story_choice_question_label) {
        lv_label_set_text(s_story_choice_question_label, "");
    }
    if (s_story_choice_button1_label) {
        lv_label_set_text(s_story_choice_button1_label, "");
    }
    if (s_story_choice_button2_label) {
        lv_label_set_text(s_story_choice_button2_label, "");
    }
}

static void ui_dual_gif_story_choice_event_cb(lv_event_t *e)
{
    const int choice_index = (int)(intptr_t)lv_event_get_user_data(e);

    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !s_story_choice_waiting) {
        return;
    }

    s_story_choice_waiting = false;
    (void)story_audio_play_cue(STORY_AUDIO_CUE_CHOICE_SELECT);
    ui_dual_gif_hide_story_choice();

    if (s_callbacks.choice_selected) {
        s_callbacks.choice_selected(choice_index, s_callbacks.user_ctx);
    }
}

static void ui_dual_gif_story_text_fade_out_timer_cb(lv_timer_t *timer)
{
    if (s_story_text_fade_out_timer == timer) {
        s_story_text_fade_out_timer = NULL;
    }

    ui_dual_gif_start_story_text_fade_out_now();
}

static void ui_dual_gif_story_choice_fade_out_timer_cb(lv_timer_t *timer)
{
    if (s_story_choice_fade_out_timer == timer) {
        s_story_choice_fade_out_timer = NULL;
    }

    ui_dual_gif_start_story_choice_fade_out_now();
}

static void ui_dual_gif_schedule_story_text_fade_in(uint32_t delay_ms)
{
    if (s_story_text_fade_in_timer) {
        lv_timer_del(s_story_text_fade_in_timer);
        s_story_text_fade_in_timer = NULL;
    }

    if (!s_story_text_view) {
        return;
    }

    if (delay_ms == 0U) {
        ui_dual_gif_start_story_text_fade_in_now();
        return;
    }

    s_story_text_fade_in_timer = lv_timer_create(ui_dual_gif_story_text_fade_in_timer_cb, delay_ms, NULL);
    if (s_story_text_fade_in_timer) {
        lv_timer_set_repeat_count(s_story_text_fade_in_timer, 1);
    }
}

static void ui_dual_gif_schedule_story_text_fade_out(uint32_t delay_ms)
{
    if (s_story_text_fade_out_timer) {
        lv_timer_del(s_story_text_fade_out_timer);
        s_story_text_fade_out_timer = NULL;
    }

    if (!s_story_text_view) {
        return;
    }

    if (delay_ms == 0U) {
        ui_dual_gif_start_story_text_fade_out_now();
        return;
    }

    s_story_text_fade_out_timer = lv_timer_create(ui_dual_gif_story_text_fade_out_timer_cb, delay_ms, NULL);
    if (s_story_text_fade_out_timer) {
        lv_timer_set_repeat_count(s_story_text_fade_out_timer, 1);
    }
}

static void ui_dual_gif_schedule_story_choice_fade_in(uint32_t delay_ms)
{
    if (s_story_choice_fade_in_timer) {
        lv_timer_del(s_story_choice_fade_in_timer);
        s_story_choice_fade_in_timer = NULL;
    }

    if (!s_story_choice_container) {
        return;
    }

    if (delay_ms == 0U) {
        ui_dual_gif_start_story_choice_fade_in_now();
        return;
    }

    s_story_choice_fade_in_timer = lv_timer_create(ui_dual_gif_story_choice_fade_in_timer_cb, delay_ms, NULL);
    if (s_story_choice_fade_in_timer) {
        lv_timer_set_repeat_count(s_story_choice_fade_in_timer, 1);
    }
}

static void ui_dual_gif_schedule_story_choice_fade_out(uint32_t delay_ms)
{
    if (s_story_choice_fade_out_timer) {
        lv_timer_del(s_story_choice_fade_out_timer);
        s_story_choice_fade_out_timer = NULL;
    }

    if (!s_story_choice_container) {
        return;
    }

    if (delay_ms == 0U) {
        ui_dual_gif_start_story_choice_fade_out_now();
        return;
    }

    s_story_choice_fade_out_timer = lv_timer_create(ui_dual_gif_story_choice_fade_out_timer_cb, delay_ms, NULL);
    if (s_story_choice_fade_out_timer) {
        lv_timer_set_repeat_count(s_story_choice_fade_out_timer, 1);
    }
}

static void ui_dual_gif_start_story_text_fade_in_now(void)
{
    lv_anim_t anim;

    if (!s_story_text_view) {
        return;
    }

    ui_dual_gif_cancel_story_text_anim();
    ui_dual_gif_apply_story_text_content_now(&s_staged_story_content, true);
    lv_obj_clear_flag(s_story_text_view, LV_OBJ_FLAG_HIDDEN);
    s_story_text_auto_scroll_offset = 0;
    ui_dual_gif_layout_story_text();
    lv_obj_scroll_to(s_story_text_view, 0, 0, LV_ANIM_OFF);
    ui_dual_gif_maybe_start_story_text_auto_scroll();

    if (UI_DUAL_GIF_STORY_TEXT_FADE_IN_MS == 0U) {
        lv_obj_set_style_opa(s_story_text_view, LV_OPA_COVER, LV_PART_MAIN);
        return;
    }

    lv_obj_set_style_opa(s_story_text_view, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_story_text_view);
    lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&anim, (int32_t)UI_DUAL_GIF_STORY_TEXT_FADE_IN_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void ui_dual_gif_start_story_text_fade_out_now(void)
{
    lv_anim_t anim;
    lv_opa_t start_opa = LV_OPA_TRANSP;

    if (!s_story_text_view) {
        return;
    }

    ui_dual_gif_cancel_story_text_anim();

    if (lv_obj_has_flag(s_story_text_view, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_style_opa(s_story_text_view, LV_OPA_TRANSP, LV_PART_MAIN);
        return;
    }

    start_opa = lv_obj_get_style_opa(s_story_text_view, LV_PART_MAIN);
    if (start_opa <= LV_OPA_TRANSP || UI_DUAL_GIF_STORY_TEXT_FADE_OUT_MS == 0U) {
        ui_dual_gif_hide_story_text();
        return;
    }

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_story_text_view);
    lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
    lv_anim_set_values(&anim, start_opa, LV_OPA_TRANSP);
    lv_anim_set_time(&anim, (int32_t)UI_DUAL_GIF_STORY_TEXT_FADE_OUT_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&anim, ui_dual_gif_story_text_fade_out_ready_cb);
    lv_anim_start(&anim);
}

static void ui_dual_gif_start_story_choice_fade_in_now(void)
{
    lv_anim_t anim;

    if (!s_story_choice_container) {
        return;
    }

    ui_dual_gif_cancel_story_choice_timers();
    ui_dual_gif_cancel_story_choice_anim();
    ui_dual_gif_show_story_img_button();
    lv_obj_clear_flag(s_story_choice_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_story_choice_container, LV_OPA_TRANSP, LV_PART_MAIN);

    if (UI_DUAL_GIF_STORY_CHOICE_FADE_IN_MS == 0U) {
        lv_obj_set_style_opa(s_story_choice_container, LV_OPA_COVER, LV_PART_MAIN);
        ui_dual_gif_show_story_img_button();
        return;
    }

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_story_choice_container);
    lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&anim, (int32_t)UI_DUAL_GIF_STORY_CHOICE_FADE_IN_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void ui_dual_gif_start_story_choice_fade_out_now(void)
{
    lv_anim_t anim;
    lv_opa_t start_opa = LV_OPA_COVER;

    if (!s_story_choice_container) {
        return;
    }

    ui_dual_gif_cancel_story_choice_timers();
    ui_dual_gif_cancel_story_choice_anim();

    if (lv_obj_has_flag(s_story_choice_container, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_style_opa(s_story_choice_container, LV_OPA_TRANSP, LV_PART_MAIN);
        return;
    }

    start_opa = lv_obj_get_style_opa(s_story_choice_container, LV_PART_MAIN);
    if (start_opa <= LV_OPA_TRANSP || UI_DUAL_GIF_STORY_CHOICE_FADE_OUT_MS == 0U) {
        lv_obj_set_style_opa(s_story_choice_container, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_add_flag(s_story_choice_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_story_choice_container);
    lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
    lv_anim_set_values(&anim, start_opa, LV_OPA_TRANSP);
    lv_anim_set_time(&anim, (int32_t)UI_DUAL_GIF_STORY_CHOICE_FADE_OUT_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&anim, ui_dual_gif_story_choice_fade_out_ready_cb);
    lv_anim_start(&anim);
}

static void ui_dual_gif_create_post_close_panels(void)
{
    uint32_t i = 0;

    for (i = 0; i < UI_DUAL_GIF_POST_CLOSE_PANEL_COUNT; ++i) {
        s_post_close_panels[i] = lv_btn_create(s_dual_gif_screen);
        lv_obj_remove_style_all(s_post_close_panels[i]);
        lv_obj_set_width(s_post_close_panels[i], UI_DUAL_GIF_POST_CLOSE_PANEL_SIZE);
        lv_obj_set_height(s_post_close_panels[i], UI_DUAL_GIF_POST_CLOSE_PANEL_SIZE);
        lv_obj_set_x(s_post_close_panels[i], s_post_close_panel_cfgs[i].x);
        lv_obj_set_y(s_post_close_panels[i], s_post_close_panel_cfgs[i].y);
        lv_obj_set_align(s_post_close_panels[i], LV_ALIGN_CENTER);
        lv_obj_add_flag(s_post_close_panels[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_post_close_panels[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(s_post_close_panels[i], UI_DUAL_GIF_POST_CLOSE_PANEL_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(s_post_close_panels[i], lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(s_post_close_panels[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(s_post_close_panels[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_color(s_post_close_panels[i], lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_opa(s_post_close_panels[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(s_post_close_panels[i], UI_DUAL_GIF_POST_CLOSE_PANEL_SHADOW_WIDTH, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_spread(s_post_close_panels[i], s_post_close_panel_cfgs[i].shadow_spread, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_opa(s_post_close_panels[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_add_event_cb(s_post_close_panels[i],
                            ui_dual_gif_cloud_button_event_cb,
                            LV_EVENT_ALL,
                            (void *)(intptr_t)(i + 1U));
    }
}

static void ui_dual_gif_draw_decor_shadow(const ui_dual_gif_decor_shadow_cfg_t *cfg)
{
    lv_draw_rect_dsc_t rect_dsc;
    lv_coord_t x = 0;
    lv_coord_t y = 0;

    if (!s_decor_bg_canvas || !cfg) {
        return;
    }

    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.radius = LV_RADIUS_CIRCLE;
    rect_dsc.bg_color = lv_color_hex(0xFAF190);
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.border_width = 0;
    rect_dsc.shadow_color = lv_color_hex(0xFAF190);
    rect_dsc.shadow_opa = LV_OPA_COVER;
    rect_dsc.shadow_width = UI_DUAL_GIF_POST_CLOSE_PANEL_SHADOW_WIDTH;
    rect_dsc.shadow_spread = cfg->shadow_spread;

    x = (BSP_LCD_H_RES / 2) + cfg->x - (cfg->width / 2) - UI_DUAL_GIF_DECOR_BG_OFFSET_X;
    y = (BSP_LCD_V_RES / 2) + cfg->y - (cfg->height / 2) - UI_DUAL_GIF_DECOR_BG_OFFSET_Y;
    lv_canvas_draw_rect(s_decor_bg_canvas, x, y, cfg->width, cfg->height, &rect_dsc);
}

static void ui_dual_gif_draw_decor_img(const ui_dual_gif_decor_img_cfg_t *cfg)
{
    const lv_img_dsc_t *img_dsc = NULL;
    lv_draw_img_dsc_t draw_dsc;
    lv_coord_t x = 0;
    lv_coord_t y = 0;

    if (!s_decor_bg_canvas || !cfg) {
        return;
    }

    img_dsc = ui_dual_gif_get_decor_asset(cfg->asset_id);
    if (!img_dsc) {
        return;
    }

    lv_draw_img_dsc_init(&draw_dsc);
    if (cfg->set_angle) {
        draw_dsc.angle = cfg->angle;
        draw_dsc.pivot.x = img_dsc->header.w / 2;
        draw_dsc.pivot.y = img_dsc->header.h / 2;
    }

    x = (BSP_LCD_H_RES / 2) + cfg->x - ((lv_coord_t)img_dsc->header.w / 2) - UI_DUAL_GIF_DECOR_BG_OFFSET_X;
    y = (BSP_LCD_V_RES / 2) + cfg->y - ((lv_coord_t)img_dsc->header.h / 2) - UI_DUAL_GIF_DECOR_BG_OFFSET_Y;
    lv_canvas_draw_img(s_decor_bg_canvas, x, y, img_dsc, &draw_dsc);
}

static void ui_dual_gif_create_background_decor(void)
{
    uint32_t buf_size = 0;
    size_t i = 0;

    if (!s_dual_gif_screen) {
        return;
    }

    if (s_decor_bg_canvas) {
        return;
    }

    buf_size = lv_img_buf_get_img_size(UI_DUAL_GIF_DECOR_BG_WIDTH,
                                       UI_DUAL_GIF_DECOR_BG_HEIGHT,
                                       LV_IMG_CF_TRUE_COLOR);
    s_decor_bg_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_decor_bg_buf) {
        s_decor_bg_buf = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT);
    }
    if (!s_decor_bg_buf) {
        ESP_LOGE(TAG, "failed to allocate decor background buffer (%u bytes)", (unsigned)buf_size);
        return;
    }

    s_decor_bg_canvas = lv_canvas_create(s_dual_gif_screen);
    lv_obj_add_event_cb(s_decor_bg_canvas,
                        ui_dual_gif_decor_canvas_delete_event_cb,
                        LV_EVENT_DELETE,
                        s_decor_bg_buf);
    lv_obj_remove_style_all(s_decor_bg_canvas);
    lv_obj_clear_flag(s_decor_bg_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_decor_bg_canvas, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(s_decor_bg_canvas, UI_DUAL_GIF_DECOR_BG_OFFSET_X, UI_DUAL_GIF_DECOR_BG_OFFSET_Y);
    lv_obj_set_size(s_decor_bg_canvas, UI_DUAL_GIF_DECOR_BG_WIDTH, UI_DUAL_GIF_DECOR_BG_HEIGHT);
    lv_canvas_set_buffer(s_decor_bg_canvas,
                         s_decor_bg_buf,
                         UI_DUAL_GIF_DECOR_BG_WIDTH,
                         UI_DUAL_GIF_DECOR_BG_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(s_decor_bg_canvas, lv_color_hex(0x171717), LV_OPA_COVER);

    for (i = 0; i < UI_DUAL_GIF_ARRAY_SIZE(s_decor_star_shadow_cfgs); ++i) {
        ui_dual_gif_draw_decor_shadow(&s_decor_star_shadow_cfgs[i]);
    }

    for (i = 0; i < UI_DUAL_GIF_ARRAY_SIZE(s_decor_star_cfgs); ++i) {
        ui_dual_gif_draw_decor_img(&s_decor_star_cfgs[i]);
    }

    for (i = 0; i < UI_DUAL_GIF_ARRAY_SIZE(s_decor_spark_cfgs); ++i) {
        ui_dual_gif_draw_decor_img(&s_decor_spark_cfgs[i]);
    }

    ui_dual_gif_release_decor_assets();
}

static void ui_dual_gif_decor_canvas_delete_event_cb(lv_event_t *e)
{
    uint8_t *deleted_buf = NULL;
    lv_obj_t *deleted_canvas = NULL;

    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }

    deleted_canvas = lv_event_get_target(e);
    deleted_buf = (uint8_t *)lv_event_get_user_data(e);
    if (deleted_canvas) {
        /* Drop the cached canvas decoder while its backing buffer is still
         * valid.  The canvas destructor will invalidate it once more, which
         * is harmless. */
        lv_img_cache_invalidate_src(lv_canvas_get_img(deleted_canvas));
    }
    if (deleted_buf) {
        heap_caps_free(deleted_buf);
    }
    if (s_decor_bg_canvas == deleted_canvas) {
        s_decor_bg_canvas = NULL;
    }
    if (s_decor_bg_buf == deleted_buf) {
        s_decor_bg_buf = NULL;
    }
}

static void ui_dual_gif_hide_cloud(void)
{
    uint32_t i = 0;

    ui_dual_gif_cancel_cloud_anim();
    ui_dual_gif_make_cloud_invisible();
    ui_dual_gif_hide_story_img_button();

    for (i = 0; i < UI_DUAL_GIF_POST_CLOSE_PANEL_COUNT; ++i) {
        if (s_post_close_panels[i]) {
            lv_obj_add_flag(s_post_close_panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_post_close_label) {
        lv_obj_add_flag(s_post_close_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_dual_gif_make_cloud_invisible(void)
{
    uint32_t i = 0;

    for (i = 0; i < UI_DUAL_GIF_POST_CLOSE_PANEL_COUNT; ++i) {
        if (s_post_close_panels[i]) {
            lv_obj_set_style_opa(s_post_close_panels[i], LV_OPA_TRANSP, LV_PART_MAIN);
        }
    }

    if (s_post_close_label) {
        lv_obj_set_style_opa(s_post_close_label, LV_OPA_TRANSP, LV_PART_MAIN);
    }
}

static void ui_dual_gif_show_cloud(void)
{
    lv_anim_t anim;
    uint32_t i = 0;

    ui_dual_gif_cancel_cloud_anim();
    ui_dual_gif_show_story_img_button();

    for (i = 0; i < UI_DUAL_GIF_POST_CLOSE_PANEL_COUNT; ++i) {
        if (s_post_close_panels[i]) {
            lv_obj_clear_flag(s_post_close_panels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(s_post_close_panels[i], LV_OPA_TRANSP, LV_PART_MAIN);

            if (UI_DUAL_GIF_POST_CLOSE_UI_FADE_IN_MS == 0U) {
                lv_obj_set_style_opa(s_post_close_panels[i], LV_OPA_COVER, LV_PART_MAIN);
                continue;
            }

            lv_anim_init(&anim);
            lv_anim_set_var(&anim, s_post_close_panels[i]);
            lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
            lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_time(&anim, (int32_t)UI_DUAL_GIF_POST_CLOSE_UI_FADE_IN_MS);
            lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
            lv_anim_start(&anim);
        }
    }

    if (s_post_close_label) {
        lv_obj_clear_flag(s_post_close_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_post_close_label, LV_OPA_TRANSP, LV_PART_MAIN);

        if (UI_DUAL_GIF_POST_CLOSE_UI_FADE_IN_MS == 0U) {
            lv_obj_set_style_opa(s_post_close_label, LV_OPA_COVER, LV_PART_MAIN);
            (void)ui_dual_gif_preload_book_path_now(UI_DUAL_GIF_OPEN_PATH);
            return;
        }

        lv_anim_init(&anim);
        lv_anim_set_var(&anim, s_post_close_label);
        lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_opa);
        lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&anim, (int32_t)UI_DUAL_GIF_POST_CLOSE_UI_FADE_IN_MS);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
    }

    (void)ui_dual_gif_preload_book_path_now(UI_DUAL_GIF_OPEN_PATH);
}

static void ui_dual_gif_cancel_cloud_anim(void)
{
    uint32_t i = 0;

    for (i = 0; i < UI_DUAL_GIF_POST_CLOSE_PANEL_COUNT; ++i) {
        if (s_post_close_panels[i]) {
            lv_anim_del(s_post_close_panels[i], ui_dual_gif_set_obj_opa);
        }
    }

    if (s_post_close_label) {
        lv_anim_del(s_post_close_label, ui_dual_gif_set_obj_opa);
    }
}

static void ui_dual_gif_cloud_button_event_cb(lv_event_t *e)
{
    const int button_index = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_RELEASED) {
        ESP_LOGI(TAG, "cloud button released: %d", button_index);
        (void)story_audio_play_cue(STORY_AUDIO_CUE_STORY_SPARKLE);
        if (s_callbacks.restart_requested) {
            s_callbacks.restart_requested(s_callbacks.user_ctx);
        }
        return;
    }

    LV_UNUSED(code);
}

static void ui_dual_gif_notify_scene_image_ready(void)
{
    if (!s_scene_ready_pending) {
        return;
    }

    s_scene_ready_pending = false;
    ui_dual_gif_show_story_img_button();
    if (s_callbacks.scene_image_ready) {
        s_callbacks.scene_image_ready(s_callbacks.user_ctx);
    }
}

static void ui_dual_gif_try_notify_scene_image_ready(void *ctx)
{
    LV_UNUSED(ctx);

    if (!s_scene_ready_pending || !s_scene_overlay_ready || !s_book_transition_ready) {
        return;
    }

    ui_dual_gif_notify_scene_image_ready();
}

static void ui_dual_gif_notify_close_finished_on_ui_thread(void *ctx)
{
    LV_UNUSED(ctx);

    if (s_book_action != UI_DUAL_GIF_BOOK_ACTION_CLOSE) {
        return;
    }

    s_book_action = UI_DUAL_GIF_BOOK_ACTION_NONE;
    ui_dual_gif_show_story_img_button();
    if (s_callbacks.close_finished) {
        s_callbacks.close_finished(s_callbacks.user_ctx);
    }
}

static void ui_dual_gif_overlay_hide_timer_cb(lv_timer_t *timer)
{
    if (s_overlay_hide_timer == timer) {
        s_overlay_hide_timer = NULL;
    }
    ui_dual_gif_finish_overlay_hide();
}

static void ui_dual_gif_overlay_image_fade_in_ready_cb(lv_anim_t *anim)
{
    LV_UNUSED(anim);
    s_scene_overlay_ready = true;
    ui_dual_gif_try_notify_scene_image_ready(NULL);
}

static void ui_dual_gif_finish_overlay_hide(void)
{
    const void *current_src = s_a_small_img ? lv_img_get_src(s_a_small_img) : NULL;

    ui_dual_gif_cancel_overlay_corner_anims();

    if (s_a_small_img) {
        lv_anim_del(s_a_small_img, ui_dual_gif_set_obj_img_opa);
        lv_obj_set_style_img_opa(s_a_small_img, LV_OPA_TRANSP, LV_PART_MAIN);
        ui_dual_gif_invalidate_overlay_src(current_src, "finish_overlay_hide_before_clear");
        lv_img_set_src(s_a_small_img, NULL);
    }

    ui_dual_gif_set_overlay_corners_opa(LV_OPA_TRANSP);

    if (s_book_overlay_layer) {
        lv_obj_add_flag(s_book_overlay_layer, LV_OBJ_FLAG_HIDDEN);
    }
    ui_dual_gif_force_overlay_redraw("finish_overlay_hide");
}

static void ui_dual_gif_show_overlay_internal(bool fade_corners, uint32_t image_delay_ms)
{
    lv_anim_t anim;
    uint32_t image_fade_delay_ms = image_delay_ms;
    const lv_img_dsc_t *next_src = s_overlay_story_img ? s_overlay_story_img : ui_dual_gif_get_default_overlay_image();
    const void *current_src = lv_img_get_src(s_a_small_img);

    if (!s_book_overlay_layer || !s_a_small_img) {
        return;
    }

    ui_dual_gif_cancel_overlay_hide_timer();
    ui_dual_gif_cancel_overlay_corner_anims();
    lv_anim_del(s_a_small_img, ui_dual_gif_set_obj_img_opa);
    lv_obj_clear_flag(s_book_overlay_layer, LV_OBJ_FLAG_HIDDEN);
    UI_DIAG_LOGI("scene_overlay_set_src case=%d node=%s uri=%s size=%u",
                 s_staged_story_content.debug_case_id,
                 s_staged_story_content.debug_node_name ? s_staged_story_content.debug_node_name : "<null>",
                 s_staged_story_content.debug_image_uri ? s_staged_story_content.debug_image_uri : "<null>",
                 s_overlay_story_img ? (unsigned)s_overlay_story_img->data_size :
                                       (ui_dual_gif_get_default_overlay_image() ?
                                        (unsigned)ui_dual_gif_get_default_overlay_image()->data_size : 0U));
    ui_dual_gif_invalidate_overlay_src(current_src, "overlay_clear_before_set_src_prev");
    lv_img_set_src(s_a_small_img, NULL);
    ui_dual_gif_force_overlay_redraw("overlay_clear_before_set_src");
    ui_dual_gif_invalidate_overlay_src(next_src, "overlay_set_src_next");
    lv_img_set_src(s_a_small_img, next_src);
    ui_dual_gif_log_overlay_runtime_state("overlay_set_src", &s_staged_story_content);
    ui_dual_gif_force_overlay_redraw("overlay_set_src");
    if (!fade_corners || UI_DUAL_GIF_CORNERS_FADE_IN_MS == 0U) {
        ui_dual_gif_set_overlay_corners_opa(LV_OPA_COVER);
    } else {
        ui_dual_gif_set_overlay_corners_opa(LV_OPA_TRANSP);
        ui_dual_gif_start_overlay_corners_fade(LV_OPA_TRANSP,
                                               LV_OPA_COVER,
                                               UI_DUAL_GIF_CORNERS_FADE_IN_MS,
                                               UI_DUAL_GIF_CORNERS_FADE_IN_DELAY_MS);
        image_fade_delay_ms += UI_DUAL_GIF_CORNERS_FADE_IN_DELAY_MS;
    }
    lv_obj_set_style_img_opa(s_a_small_img, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_a_small_img);
    lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_img_opa);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_delay(&anim, (int32_t)image_fade_delay_ms);
    lv_anim_set_time(&anim, (int32_t)UI_DUAL_GIF_OVERLAY_IMAGE_FADE_IN_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&anim, ui_dual_gif_overlay_image_fade_in_ready_cb);
    lv_anim_start(&anim);
}

static void ui_dual_gif_hide_overlay_internal(uint32_t corner_delay_ms)
{
    lv_anim_t anim;
    const lv_opa_t a_small_start_opa = s_a_small_img ?
                                       lv_obj_get_style_img_opa(s_a_small_img, LV_PART_MAIN) :
                                       LV_OPA_TRANSP;
    const lv_opa_t corners_start_opa = ui_dual_gif_get_overlay_corners_opa();
    uint32_t max_fade_ms = 0;

    if (!s_book_overlay_layer || !s_a_small_img) {
        return;
    }

    ui_dual_gif_cancel_overlay_hide_timer();
    ui_dual_gif_cancel_overlay_corner_anims();
    lv_anim_del(s_a_small_img, ui_dual_gif_set_obj_img_opa);
    if (lv_obj_has_flag(s_book_overlay_layer, LV_OBJ_FLAG_HIDDEN)) {
        ui_dual_gif_reset_overlay_state();
        return;
    }

    if (a_small_start_opa <= LV_OPA_TRANSP && corners_start_opa <= LV_OPA_TRANSP) {
        ui_dual_gif_reset_overlay_state();
        return;
    }

    if (corners_start_opa > LV_OPA_TRANSP) {
        if (UI_DUAL_GIF_CORNERS_FADE_OUT_MS == 0U) {
            if (corner_delay_ms == 0U) {
                ui_dual_gif_set_overlay_corners_opa(LV_OPA_TRANSP);
            }
            max_fade_ms = corner_delay_ms;
        } else {
            ui_dual_gif_start_overlay_corners_fade(corners_start_opa,
                                                   LV_OPA_TRANSP,
                                                   UI_DUAL_GIF_CORNERS_FADE_OUT_MS,
                                                   corner_delay_ms);
            max_fade_ms = corner_delay_ms + UI_DUAL_GIF_CORNERS_FADE_OUT_MS;
        }
    }

    if (a_small_start_opa > LV_OPA_TRANSP) {
        if (UI_DUAL_GIF_OVERLAY_IMAGE_FADE_OUT_MS == 0U) {
            lv_obj_set_style_img_opa(s_a_small_img, LV_OPA_TRANSP, LV_PART_MAIN);
        } else {
            lv_anim_init(&anim);
            lv_anim_set_var(&anim, s_a_small_img);
            lv_anim_set_exec_cb(&anim, ui_dual_gif_set_obj_img_opa);
            lv_anim_set_values(&anim, a_small_start_opa, LV_OPA_TRANSP);
            lv_anim_set_time(&anim, (int32_t)UI_DUAL_GIF_OVERLAY_IMAGE_FADE_OUT_MS);
            lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
            lv_anim_start(&anim);
            if (UI_DUAL_GIF_OVERLAY_IMAGE_FADE_OUT_MS > max_fade_ms) {
                max_fade_ms = UI_DUAL_GIF_OVERLAY_IMAGE_FADE_OUT_MS;
            }
        }
    }

    if (max_fade_ms == 0U) {
        ui_dual_gif_reset_overlay_state();
        return;
    }

    s_overlay_hide_timer = lv_timer_create(ui_dual_gif_overlay_hide_timer_cb, max_fade_ms + 20U, NULL);
    if (s_overlay_hide_timer) {
        lv_timer_set_repeat_count(s_overlay_hide_timer, 1);
    }
}

static void ui_dual_gif_swipe_overlay_hide_timer_cb(lv_timer_t *timer)
{
    if (s_overlay_swipe_hide_timer == timer) {
        s_overlay_swipe_hide_timer = NULL;
    }

    ui_dual_gif_hide_overlay_internal(0U);
}

static void ui_dual_gif_swipe_overlay_show_timer_cb(lv_timer_t *timer)
{
    if (s_overlay_swipe_show_timer == timer) {
        s_overlay_swipe_show_timer = NULL;
    }

    ui_dual_gif_show_overlay_internal(true,
                                      UI_DUAL_GIF_CORNERS_FADE_IN_MS + UI_DUAL_GIF_SWIPE_NEXT_IMAGE_FADE_IN_DELAY_MS);
}

static void ui_dual_gif_start_overlay_stage_sequence(const lv_img_dsc_t *story_img,
                                                     uint32_t show_delay_ms,
                                                     uint32_t hide_delay_ms,
                                                     bool schedule_hide,
                                                     bool show_story_text)
{
    uint32_t text_fade_in_delay_ms = show_delay_ms + UI_DUAL_GIF_STORY_TEXT_FADE_IN_DELAY_MS;

    if (!s_book_overlay_layer || !s_a_small_img) {
        return;
    }

    ui_dual_gif_cancel_overlay_swipe_timers();
    ui_dual_gif_cancel_overlay_hide_timer();
    ui_dual_gif_cancel_story_text_timers();
    ui_dual_gif_cancel_story_text_anim();
    ui_dual_gif_set_overlay_story_image(story_img);
    ui_dual_gif_hide_story_choice();

    if (schedule_hide) {
        ui_dual_gif_schedule_story_text_fade_out(UI_DUAL_GIF_STORY_TEXT_SWIPE_FADE_OUT_DELAY_MS);
        if (show_story_text) {
            ui_dual_gif_schedule_story_text_fade_in(UI_DUAL_GIF_STORY_TEXT_SWIPE_FADE_IN_DELAY_MS);
        }
    } else if (show_story_text) {
        if (s_first_open_text_delay_pending &&
            s_staged_story_content.reading_text &&
            s_staged_story_content.reading_text[0] != '\0') {
            text_fade_in_delay_ms += UI_DUAL_GIF_STORY_TEXT_FIRST_OPEN_EXTRA_DELAY_MS;
            s_first_open_text_delay_pending = false;
        }

        ui_dual_gif_schedule_story_text_fade_in(text_fade_in_delay_ms);
    }

    if (show_delay_ms == 0U) {
        ui_dual_gif_show_overlay_internal(true,
                                          UI_DUAL_GIF_CORNERS_FADE_IN_MS +
                                          UI_DUAL_GIF_SWIPE_NEXT_IMAGE_FADE_IN_DELAY_MS);
    } else {
        s_overlay_swipe_show_timer = lv_timer_create(ui_dual_gif_swipe_overlay_show_timer_cb,
                                                     show_delay_ms,
                                                     NULL);
        if (s_overlay_swipe_show_timer) {
            lv_timer_set_repeat_count(s_overlay_swipe_show_timer, 1);
        }
    }

    if (schedule_hide) {
        s_overlay_swipe_hide_timer = lv_timer_create(ui_dual_gif_swipe_overlay_hide_timer_cb,
                                                     hide_delay_ms,
                                                     NULL);
        if (s_overlay_swipe_hide_timer) {
            lv_timer_set_repeat_count(s_overlay_swipe_hide_timer, 1);
        }
    }
}

static void ui_dual_gif_hide_overlay(void)
{
    ui_dual_gif_cancel_overlay_swipe_timers();
    ui_dual_gif_cancel_story_text_timers();
    ui_dual_gif_schedule_story_text_fade_out(UI_DUAL_GIF_STORY_TEXT_FADE_OUT_DELAY_MS);
    ui_dual_gif_hide_story_choice();
    ui_dual_gif_hide_overlay_internal(UI_DUAL_GIF_CORNERS_FADE_OUT_DELAY_MS);
}

static void ui_dual_gif_start_entry_animation(ui_dual_gif_player_t *player, uint32_t delay_ms)
{
    lv_anim_t anim_x;
    lv_anim_t anim_y;

    if (!player || !player->img_obj || !player->animate_entry || player->entry_anim_started) {
        return;
    }

    lv_anim_del(player->img_obj, ui_dual_gif_set_obj_translate_x);
    lv_anim_del(player->img_obj, ui_dual_gif_set_obj_translate_y);
    lv_obj_set_style_translate_x(player->img_obj, player->entry_start_shift_x, LV_PART_MAIN);
    lv_obj_set_style_translate_y(player->img_obj, player->entry_start_shift_y, LV_PART_MAIN);

    lv_anim_init(&anim_x);
    lv_anim_set_var(&anim_x, player->img_obj);
    lv_anim_set_exec_cb(&anim_x, ui_dual_gif_set_obj_translate_x);
    lv_anim_set_values(&anim_x, player->entry_start_shift_x, 0);
    lv_anim_set_delay(&anim_x, (int32_t)delay_ms);
    lv_anim_set_time(&anim_x, (int32_t)player->entry_duration_ms);
    lv_anim_set_path_cb(&anim_x, lv_anim_path_linear);
    lv_anim_start(&anim_x);

    lv_anim_init(&anim_y);
    lv_anim_set_var(&anim_y, player->img_obj);
    lv_anim_set_exec_cb(&anim_y, ui_dual_gif_set_obj_translate_y);
    lv_anim_set_values(&anim_y, player->entry_start_shift_y, 0);
    lv_anim_set_delay(&anim_y, (int32_t)delay_ms);
    lv_anim_set_time(&anim_y, (int32_t)player->entry_duration_ms);
    lv_anim_set_path_cb(&anim_y, lv_anim_path_linear);
    lv_anim_start(&anim_y);

    player->entry_anim_started = true;
}

static void ui_dual_gif_try_start_bird_entry_animation(void)
{
    ui_dual_gif_player_t *player = &s_bird_player;
    uint32_t remaining_delay_ms = 0;

    if (!s_book_animation_started_us || !player->animate_entry || player->entry_anim_started ||
        !player->img_obj || !player->source_attached) {
        return;
    }

    if (player->entry_delay_ms > 0U) {
        const uint64_t now_us = (uint64_t)esp_timer_get_time();
        const uint32_t elapsed_ms = now_us > s_book_animation_started_us ?
                                    (uint32_t)((now_us - s_book_animation_started_us) / 1000U) :
                                    0U;

        remaining_delay_ms = elapsed_ms < player->entry_delay_ms ?
                             (player->entry_delay_ms - elapsed_ms) :
                             0U;
    }

    ui_dual_gif_start_entry_animation(player, remaining_delay_ms);
}

static void ui_dual_gif_start_bird_return_animation(void)
{
    ui_dual_gif_player_t *player = &s_bird_player;
    lv_anim_t anim_x;
    lv_coord_t current_translate_x = 0;
    lv_coord_t current_translate_y = 0;

    if (!player->animate_entry || !player->img_obj || !player->source_attached) {
        return;
    }

    current_translate_x = lv_obj_get_style_translate_x(player->img_obj, LV_PART_MAIN);
    current_translate_y = lv_obj_get_style_translate_y(player->img_obj, LV_PART_MAIN);

    lv_anim_del(player->img_obj, ui_dual_gif_set_obj_translate_x);
    lv_anim_del(player->img_obj, ui_dual_gif_set_obj_translate_y);
    lv_obj_set_style_translate_y(player->img_obj, current_translate_y, LV_PART_MAIN);

    lv_anim_init(&anim_x);
    lv_anim_set_var(&anim_x, player->img_obj);
    lv_anim_set_exec_cb(&anim_x, ui_dual_gif_set_obj_translate_x);
    lv_anim_set_values(&anim_x, current_translate_x, player->entry_start_shift_x);
    lv_anim_set_delay(&anim_x, (int32_t)player->return_delay_ms);
    lv_anim_set_time(&anim_x, (int32_t)player->return_duration_ms);
    lv_anim_set_path_cb(&anim_x, lv_anim_path_linear);
    lv_anim_start(&anim_x);
}

static void ui_dual_gif_finish_bird_return_height(void)
{
    ui_dual_gif_player_t *player = &s_bird_player;

    if (!player->animate_entry || !player->img_obj || !player->source_attached) {
        return;
    }

    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "LVGL lock timed out while finalizing bird return height");
        return;
    }

    lv_anim_del(player->img_obj, ui_dual_gif_set_obj_translate_y);
    lv_obj_set_style_translate_y(player->img_obj, player->entry_start_shift_y, LV_PART_MAIN);
    lv_obj_invalidate(player->img_obj);
    lvgl_port_unlock();
}

static void ui_dual_gif_mark_book_animation_started(void)
{
    if (s_book_animation_started_us != 0U) {
        return;
    }

    s_book_animation_started_us = (uint64_t)esp_timer_get_time();
    ui_dual_gif_start_overlay_stage_sequence(ui_dual_gif_get_default_overlay_image(),
                                             UI_DUAL_GIF_OPEN_OVERLAY_SHOW_DELAY_MS,
                                             0U,
                                             false,
                                             true);
    ui_dual_gif_try_start_bird_entry_animation();
}

static void ui_dual_gif_align_story_layers(void)
{
    if (s_book_overlay_layer) {
        lv_obj_align(s_book_overlay_layer,
                     LV_ALIGN_CENTER,
                     UI_DUAL_GIF_BOOK_OFFSET_X + UI_DUAL_GIF_BOOK_OVERLAY_OFFSET_X,
                     UI_DUAL_GIF_BOOK_OFFSET_Y + UI_DUAL_GIF_BOOK_OVERLAY_OFFSET_Y);
    }
}

static void ui_dual_gif_invalidate_area(ui_dual_gif_player_t *player, const lv_area_t *area)
{
    lv_area_t abs = {0};

    if (!player || !player->img_obj || !ui_dual_gif_area_is_valid(area)) {
        return;
    }

    abs.x1 = player->img_obj->coords.x1 + area->x1;
    abs.y1 = player->img_obj->coords.y1 + area->y1;
    abs.x2 = player->img_obj->coords.x1 + area->x2;
    abs.y2 = player->img_obj->coords.y1 + area->y2;
    lv_obj_invalidate_area(player->img_obj, &abs);
}

static void ui_dual_gif_record_stat(uint64_t *sum, uint64_t *peak, uint64_t value)
{
    if (!sum || !peak) {
        return;
    }

    *sum += value;
    if (value > *peak) {
        *peak = value;
    }
}

static void ui_dual_gif_reset_stats(ui_dual_gif_player_t *player, uint64_t first_present_us)
{
    if (!player) {
        return;
    }

    memset(&player->stats, 0, sizeof(player->stats));
    player->stats.frame_count = 1U;
    player->stats.playback_started_us = first_present_us;
    player->stats.last_present_us = first_present_us;
}

static void ui_dual_gif_record_scheduled_delay(ui_dual_gif_player_t *player,
                                               uint16_t raw_delay_cs,
                                               uint32_t effective_delay_ms)
{
    ui_dual_gif_stats_t *stats = NULL;

    if (!player) {
        return;
    }

    stats = &player->stats;
    stats->scheduled_delay_count++;
    stats->scheduled_delay_ms_total += effective_delay_ms;
    if (stats->scheduled_delay_ms_min == 0U ||
        effective_delay_ms < stats->scheduled_delay_ms_min) {
        stats->scheduled_delay_ms_min = effective_delay_ms;
    }
    if (effective_delay_ms > stats->scheduled_delay_ms_max) {
        stats->scheduled_delay_ms_max = effective_delay_ms;
    }
    if (raw_delay_cs < 3U) {
        stats->clamped_delay_frames++;
    }
}

static void ui_dual_gif_record_present_interval(ui_dual_gif_player_t *player, uint64_t presented_us)
{
    ui_dual_gif_stats_t *stats = NULL;
    uint64_t interval_us = 0;

    if (!player) {
        return;
    }

    stats = &player->stats;
    if (stats->last_present_us > 0U && presented_us > stats->last_present_us) {
        interval_us = presented_us - stats->last_present_us;
        stats->present_interval_us_total += interval_us;
        if (interval_us > stats->present_interval_us_max) {
            stats->present_interval_us_max = interval_us;
        }
    }
    stats->last_present_us = presented_us;
}

static void ui_dual_gif_record_cfa_profile(ui_dual_gif_player_t *player)
{
    animation_decoder_cfa_frame_profile_t profile = {0};
    ui_dual_gif_stats_t *stats = NULL;

    if (!player || !player->gif ||
        animation_decoder_cfa_last_profile(player->gif, &profile) != ESP_OK) {
        return;
    }

    stats = &player->stats;
    ui_dual_gif_record_stat(&stats->cfa_read_us_total, &stats->cfa_read_us_max, profile.read_us);
    ui_dual_gif_record_stat(&stats->cfa_crc_us_total, &stats->cfa_crc_us_max, profile.crc_us);
    ui_dual_gif_record_stat(&stats->cfa_decompress_us_total, &stats->cfa_decompress_us_max, profile.decompress_us);
    ui_dual_gif_record_stat(&stats->cfa_total_us_total, &stats->cfa_total_us_max, profile.total_us);
    stats->cfa_raw_size_total += profile.raw_size;
    stats->cfa_payload_size_total += profile.payload_size;
    stats->cfa_rect_pixels_total += profile.rect_pixels;
    if (profile.raw_size > stats->cfa_raw_size_max) {
        stats->cfa_raw_size_max = profile.raw_size > UINT32_MAX ? UINT32_MAX : (uint32_t)profile.raw_size;
    }
    if (profile.payload_size > stats->cfa_payload_size_max) {
        stats->cfa_payload_size_max = profile.payload_size > UINT32_MAX ? UINT32_MAX : (uint32_t)profile.payload_size;
    }
    if (profile.rect_pixels > stats->cfa_rect_pixels_max) {
        stats->cfa_rect_pixels_max = profile.rect_pixels > UINT32_MAX ? UINT32_MAX : (uint32_t)profile.rect_pixels;
    }
    if (profile.codec < UI_DUAL_GIF_ARRAY_SIZE(stats->cfa_codec_frames)) {
        stats->cfa_codec_frames[profile.codec]++;
    }
    if (profile.preloaded) {
        stats->cfa_preloaded_frames++;
    } else {
        stats->cfa_streaming_frames++;
    }
}

static void ui_dual_gif_log_cfa_stats(ui_dual_gif_player_t *player, const char *prefix)
{
    const ui_dual_gif_stats_t *stats = NULL;
    uint32_t frames = 0;

    if (!player || !player->path || !ui_dual_gif_decoder_is_cfa(player->gif)) {
        return;
    }

    stats = &player->stats;
    frames = stats->frame_count;
    if (frames == 0U) {
        return;
    }

    ESP_LOGI(TAG,
             "%s cfa=%s frames=%" PRIu32
             " read_avg=%llu us read_max=%llu us crc_avg=%llu us crc_max=%llu us"
             " decompress_avg=%llu us decompress_max=%llu us cfa_total_avg=%llu us cfa_total_max=%llu us"
             " rect_px_avg=%llu rect_px_max=%" PRIu32
             " raw_avg=%llu raw_max=%" PRIu32 " payload_avg=%llu payload_max=%" PRIu32
             " codec_raw=%" PRIu32 " codec_rle=%" PRIu32 " codec_lz4=%" PRIu32
             " preloaded=%" PRIu32 " streaming=%" PRIu32,
             prefix ? prefix : "profile",
             player->path,
             frames,
             (unsigned long long)(stats->cfa_read_us_total / frames),
             (unsigned long long)stats->cfa_read_us_max,
             (unsigned long long)(stats->cfa_crc_us_total / frames),
             (unsigned long long)stats->cfa_crc_us_max,
             (unsigned long long)(stats->cfa_decompress_us_total / frames),
             (unsigned long long)stats->cfa_decompress_us_max,
             (unsigned long long)(stats->cfa_total_us_total / frames),
             (unsigned long long)stats->cfa_total_us_max,
             (unsigned long long)(stats->cfa_rect_pixels_total / frames),
             stats->cfa_rect_pixels_max,
             (unsigned long long)(stats->cfa_raw_size_total / frames),
             stats->cfa_raw_size_max,
             (unsigned long long)(stats->cfa_payload_size_total / frames),
             stats->cfa_payload_size_max,
             stats->cfa_codec_frames[0],
             stats->cfa_codec_frames[1],
             stats->cfa_codec_frames[2],
             stats->cfa_preloaded_frames,
             stats->cfa_streaming_frames);
}

static void ui_dual_gif_log_playback_finished(ui_dual_gif_player_t *player, const char *reason)
{
    const ui_dual_gif_stats_t *stats = NULL;
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    uint32_t elapsed_ms = 0U;
    uint32_t ratio_pct = 0U;
    uint32_t frames = 0U;

    if (!player || !player->path) {
        return;
    }

    stats = &player->stats;
    frames = stats->frame_count;
    elapsed_ms = stats->playback_started_us > 0U ?
                 (uint32_t)((now_us - stats->playback_started_us) / 1000U) :
                 0U;
    ratio_pct = stats->scheduled_delay_ms_total > 0U ?
                (uint32_t)(((uint64_t)elapsed_ms * 100U) / stats->scheduled_delay_ms_total) :
                0U;

    UI_DIAG_LOGI("gif_done path=%s reason=%s frames=%" PRIu32 " scheduled_ms=%" PRIu32
                 " elapsed_ms=%" PRIu32 " ratio_pct=%" PRIu32
                 " delay_min=%" PRIu32 " delay_max=%" PRIu32 " clamped=%" PRIu32
                 " missed=%" PRIu32
                 " get_frame_avg=%llu us pixels_avg=%llu us lock_avg=%llu us",
                 player->path,
                 reason ? reason : "finished",
                 frames,
                 stats->scheduled_delay_ms_total,
                 elapsed_ms,
                 ratio_pct,
                 stats->scheduled_delay_ms_min,
                 stats->scheduled_delay_ms_max,
                 stats->clamped_delay_frames,
                 stats->missed_deadlines,
                 frames > 0U ? (unsigned long long)(stats->gd_get_frame_us_total / frames) : 0ULL,
                 frames > 0U ? (unsigned long long)(stats->pixel_update_us_total / frames) : 0ULL,
                 frames > 0U ? (unsigned long long)(stats->lvgl_lock_us_total / frames) : 0ULL);
    ui_dual_gif_log_cfa_stats(player, "cfa_done");
}

static void ui_dual_gif_schedule_next_preload(ui_dual_gif_player_t *player)
{
    ui_dual_gif_preload_job_t *job = NULL;
    media_loader_request_t request = {0};
    size_t next_index = 0;
    const char *next_path = NULL;

    if (!player || !player->sequence_paths || player->sequence_count <= 1U) {
        return;
    }

    next_index = player->sequence_index + 1U;
    if (next_index >= player->sequence_count) {
        ui_dual_gif_cancel_preload(player, UI_DUAL_GIF_PRELOAD_STOP_WAIT_MS);
        ui_dual_gif_clear_preloaded_file(player);
        return;
    }

    next_path = player->sequence_paths[next_index];
    if (player->preloaded_gif &&
        player->preloaded_path &&
        ui_dual_gif_paths_match(player->preloaded_path, next_path)) {
        return;
    }

    if (ui_dual_gif_has_cached_animation(next_path)) {
        return;
    }

    if (player->preload_enqueued &&
        player->preload_target_path &&
        ui_dual_gif_paths_match(player->preload_target_path, next_path) &&
        player->preload_target_index == next_index) {
        return;
    }

    ui_dual_gif_cancel_preload(player, UI_DUAL_GIF_PRELOAD_STOP_WAIT_MS);
    ui_dual_gif_clear_preloaded_file(player);

    job = malloc(sizeof(*job));
    if (!job) {
        ESP_LOGW(TAG, "preload job allocation failed for %s", next_path);
        return;
    }

    job->player = player;
    job->path = next_path;
    job->sequence_index = next_index;
    job->generation = ++player->preload_generation;

    player->preload_target_path = next_path;
    player->preload_target_index = next_index;
    player->preload_enqueued = true;

    request.debug_tag = "anim_preload_seq";
    request.ctx = job;
    request.generation = job->generation;
    request.priority = player == &s_book_player ?
                       MEDIA_LOADER_PRIORITY_NEXT_TRANSITION :
                       MEDIA_LOADER_PRIORITY_IDLE_PREPARE;
    request.run_cb = ui_dual_gif_preload_job_run;
    request.stale_cb = ui_dual_gif_preload_job_stale;
    request.free_cb = ui_dual_gif_preload_job_free;

    if (media_loader_service_enqueue(&request) != ESP_OK) {
        ESP_LOGW(TAG, "unable to enqueue preload job for %s", next_path);
        player->preload_target_path = NULL;
        player->preload_target_index = 0;
        player->preload_enqueued = false;
        ui_dual_gif_preload_job_free(job);
        return;
    }

    ESP_LOGI(TAG,
             "preloading gif seq %u/%u: %s",
             (unsigned)(next_index + 1U),
             (unsigned)player->sequence_count,
             next_path);
}

static void ui_dual_gif_log_stats(ui_dual_gif_player_t *player)
{
    const ui_dual_gif_stats_t *stats = NULL;
    uint32_t frames = 0;
    uint32_t delay_avg_ms = 0;
    uint64_t interval_avg_us = 0;
    uint32_t interval_count = 0;

    if (!player) {
        return;
    }

    stats = &player->stats;
    frames = stats->frame_count;
    if (frames == 0 || (frames % UI_DUAL_GIF_PROFILE_LOG_EVERY) != 0) {
        return;
    }
    delay_avg_ms = stats->scheduled_delay_count > 0U ?
                   (stats->scheduled_delay_ms_total / stats->scheduled_delay_count) :
                   0U;
    interval_count = frames > 1U ? frames - 1U : 0U;
    interval_avg_us = interval_count > 0U ?
                      (stats->present_interval_us_total / interval_count) :
                      0U;

    UI_DIAG_LOGI("gif=%s frames=%" PRIu32 " get_frame_avg=%llu us get_frame_max=%llu us "
                 "pixels_avg=%llu us pixels_max=%llu us lock_avg=%llu us lock_max=%llu us "
                 "delay_avg=%" PRIu32 " ms delay_min=%" PRIu32 " delay_max=%" PRIu32
                 " interval_avg=%llu us interval_max=%llu us missed=%" PRIu32
                 " clamped=%" PRIu32 " lct=%" PRIu32 " free_internal=%u free_psram=%u",
                 player->path,
                 frames,
                 (unsigned long long)(stats->gd_get_frame_us_total / frames),
                 (unsigned long long)stats->gd_get_frame_us_max,
                 (unsigned long long)(stats->pixel_update_us_total / frames),
                 (unsigned long long)stats->pixel_update_us_max,
                 (unsigned long long)(stats->lvgl_lock_us_total / frames),
                 (unsigned long long)stats->lvgl_lock_us_max,
                 delay_avg_ms,
                 stats->scheduled_delay_ms_min,
                 stats->scheduled_delay_ms_max,
                 (unsigned long long)interval_avg_us,
                 (unsigned long long)stats->present_interval_us_max,
                 stats->missed_deadlines,
                 stats->clamped_delay_frames,
                 stats->local_palette_frames,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ui_dual_gif_log_cfa_stats(player, "cfa_profile");
}

static bool ui_dual_gif_present_frame(ui_dual_gif_player_t *player,
                                      bool full_init,
                                      uint64_t *pixel_update_us,
                                      uint64_t *lvgl_lock_us)
{
    lv_area_t invalidate_area = {0};

    if (pixel_update_us) {
        *pixel_update_us = 0;
    }
    if (lvgl_lock_us) {
        *lvgl_lock_us = 0;
    }

    if (!player) {
        return false;
    }

    if (player->render_under_lvgl_lock) {
        const uint64_t lock_started = (uint64_t)esp_timer_get_time();

        if (!lvgl_port_lock(1000)) {
            ESP_LOGW(TAG, "LVGL lock timed out while rendering %s", player->path);
            if (lvgl_lock_us) {
                *lvgl_lock_us = (uint64_t)esp_timer_get_time() - lock_started;
            }
            return true;
        }

        if (!ui_dual_gif_render_current_frame(player, &invalidate_area, pixel_update_us, full_init)) {
            lvgl_port_unlock();
            return false;
        }

        if (ui_dual_gif_area_is_valid(&invalidate_area) && player->img_obj) {
            ui_dual_gif_invalidate_area(player, &invalidate_area);
        }

        lvgl_port_unlock();

        if (lvgl_lock_us) {
            *lvgl_lock_us = (uint64_t)esp_timer_get_time() - lock_started;
        }

        return true;
    }

    if (!ui_dual_gif_render_current_frame(player, &invalidate_area, pixel_update_us, full_init)) {
        return false;
    }

    if (ui_dual_gif_area_is_valid(&invalidate_area) && player->img_obj) {
        const uint64_t lock_started = (uint64_t)esp_timer_get_time();

        if (!lvgl_port_lock(1000)) {
            ESP_LOGW(TAG, "LVGL lock timed out while invalidating %s", player->path);
            if (lvgl_lock_us) {
                *lvgl_lock_us = (uint64_t)esp_timer_get_time() - lock_started;
            }
            return true;
        }

        ui_dual_gif_sync_rgb565a8_area(player, &invalidate_area);
        ui_dual_gif_invalidate_area(player, &invalidate_area);
        lvgl_port_unlock();

        if (lvgl_lock_us) {
            *lvgl_lock_us = (uint64_t)esp_timer_get_time() - lock_started;
        }
    }

    return true;
}

static bool ui_dual_gif_present_frame_on_ui_thread(ui_dual_gif_player_t *player,
                                                   bool full_init,
                                                   uint64_t *pixel_update_us)
{
    lv_area_t invalidate_area = {0};

    if (pixel_update_us) {
        *pixel_update_us = 0;
    }

    if (!player) {
        return false;
    }

    if (!ui_dual_gif_render_current_frame(player, &invalidate_area, pixel_update_us, full_init)) {
        return false;
    }

    if (ui_dual_gif_area_is_valid(&invalidate_area) && player->img_obj) {
        if (player->img_buf != player->render_buf && !player->use_true_color_alpha) {
            ui_dual_gif_sync_rgb565a8_area(player, &invalidate_area);
        }
        ui_dual_gif_invalidate_area(player, &invalidate_area);
    }

    return true;
}

static bool ui_dual_gif_render_current_cfa_frame(ui_dual_gif_player_t *player,
                                                 lv_area_t *invalidate_area,
                                                 uint64_t *pixel_update_us,
                                                 bool full_init)
{
    enum {
        UI_DUAL_GIF_CFA_PREV_DELTA = 4U,
        UI_DUAL_GIF_CFA_PREV_KEYFRAME = 5U,
    };
    animation_decoder_rendered_frame_view_t view = {0};
    const bool decoder_ready = player && player->gif &&
                               animation_decoder_rendered_frame_view(player->gif, &view) == ESP_OK;
    const bool empty_frame = (view.flags & ANIMATION_DECODER_RENDERED_FRAME_EMPTY) != 0U;
    const bool clear_canvas = full_init ||
                              (view.flags & ANIMATION_DECODER_RENDERED_FRAME_CLEAR_BEFORE_DRAW) != 0U ||
                              (view.flags & ANIMATION_DECODER_RENDERED_FRAME_KEYFRAME) != 0U;
    const bool has_rect = !empty_frame && view.frame_width > 0U && view.frame_height > 0U;
    const bool prev_keyframe_rect_valid = player && player->prev_rect_valid &&
                                          player->prev_disposal == UI_DUAL_GIF_CFA_PREV_KEYFRAME;
    lv_area_t current_area = {0};
    uint64_t started = 0;
    uint16_t row = 0;

    if (!decoder_ready || !player->lv_buf || !player->alpha_buf || !player->render_buf ||
        view.canvas_width != player->render_w || view.canvas_height != player->render_h) {
        return false;
    }

    if (has_rect) {
        const size_t expected_px = (size_t)view.frame_width * (size_t)view.frame_height;

        if (!view.rgb565 || !view.alpha || view.pixel_count != expected_px ||
            (uint32_t)view.frame_x + view.frame_width > player->render_w ||
            (uint32_t)view.frame_y + view.frame_height > player->render_h) {
            return false;
        }

        current_area.x1 = (lv_coord_t)view.frame_x;
        current_area.y1 = (lv_coord_t)view.frame_y;
        current_area.x2 = (lv_coord_t)(view.frame_x + view.frame_width) - 1;
        current_area.y2 = (lv_coord_t)(view.frame_y + view.frame_height) - 1;
    }

    if (pixel_update_us) {
        *pixel_update_us = 0;
    }

    started = (uint64_t)esp_timer_get_time();

    if (clear_canvas) {
        memset(player->render_buf, 0, player->render_buf_size);
        player->restore_snapshot_valid = false;
    }

    if (has_rect) {
        const uint32_t rect_width = view.frame_width;

        for (row = 0; row < view.frame_height; ++row) {
            const uint32_t dst_off = ((uint32_t)view.frame_y + row) * player->render_w + view.frame_x;
            const uint32_t src_px_off = (uint32_t)row * rect_width;

            memcpy(player->lv_buf + dst_off,
                   view.rgb565 + (src_px_off * sizeof(lv_color_t)),
                   (size_t)rect_width * sizeof(lv_color_t));
            memcpy(player->alpha_buf + dst_off,
                   view.alpha + src_px_off,
                   rect_width);
        }
    }

    if (player->use_true_color_alpha) {
        ui_dual_gif_pack_true_color_alpha(player);
    }

    if (invalidate_area) {
        if (full_init || player->invalidate_full_frame) {
            invalidate_area->x1 = 0;
            invalidate_area->y1 = 0;
            invalidate_area->x2 = (lv_coord_t)player->render_w - 1;
            invalidate_area->y2 = (lv_coord_t)player->render_h - 1;
        } else if (clear_canvas) {
            if (prev_keyframe_rect_valid && has_rect) {
                *invalidate_area = ui_dual_gif_union_area(player->prev_frame_area, current_area);
            } else if (prev_keyframe_rect_valid) {
                *invalidate_area = player->prev_frame_area;
            } else {
                invalidate_area->x1 = 0;
                invalidate_area->y1 = 0;
                invalidate_area->x2 = (lv_coord_t)player->render_w - 1;
                invalidate_area->y2 = (lv_coord_t)player->render_h - 1;
            }
        } else if (has_rect) {
            *invalidate_area = current_area;
        } else {
            ui_dual_gif_mark_area_invalid(invalidate_area);
        }
    }

    player->prev_frame_area = current_area;
    player->prev_rect_valid = has_rect;
    player->prev_disposal = clear_canvas ? UI_DUAL_GIF_CFA_PREV_KEYFRAME : UI_DUAL_GIF_CFA_PREV_DELTA;
    player->restore_snapshot_valid = false;

    if (pixel_update_us) {
        *pixel_update_us = (uint64_t)esp_timer_get_time() - started;
    }

    return true;
}

static bool ui_dual_gif_render_current_frame(ui_dual_gif_player_t *player,
                                             lv_area_t *invalidate_area,
                                             uint64_t *pixel_update_us,
                                             bool full_init)
{
    animation_decoder_frame_view_t frame_view = {0};
    const bool restore_previous = player && player->prev_rect_valid &&
                                  (player->prev_disposal == 2U || player->prev_disposal == 3U);
    const bool local_palette = ui_dual_gif_use_local_palette(player);
    const lv_area_t current_area = ui_dual_gif_current_frame_area(player);
    uint64_t started = 0;

    if (ui_dual_gif_decoder_is_cfa(player ? player->gif : NULL)) {
        return ui_dual_gif_render_current_cfa_frame(player, invalidate_area, pixel_update_us, full_init);
    }

    if (!player || !player->gif || !player->lv_buf || !player->alpha_buf ||
        animation_decoder_frame_view(player->gif, &frame_view) != ESP_OK ||
        !ui_dual_gif_area_is_valid(&current_area)) {
        return false;
    }

    if (pixel_update_us) {
        *pixel_update_us = 0;
    }

    started = (uint64_t)esp_timer_get_time();
    ui_dual_gif_prepare_palette(player);

    if (full_init) {
        memset(player->render_buf, 0, player->render_buf_size);
        if (player->img_buf != player->render_buf) {
            memset(player->img_buf, 0, player->img_buf_size);
        }
        player->restore_snapshot_valid = false;
    } else if (restore_previous) {
        if (player->prev_disposal == 2U) {
            ui_dual_gif_clear_area_transparent(player, &player->prev_frame_area);
        } else {
            ui_dual_gif_restore_snapshot(player);
        }
    }

    if (frame_view.disposal == 3U) {
        if (!ui_dual_gif_save_restore_snapshot(player, &current_area)) {
            return false;
        }
    } else {
        player->restore_snapshot_valid = false;
    }

    ui_dual_gif_overlay_frame_area(player, &current_area);
    if (player->use_true_color_alpha) {
        ui_dual_gif_pack_true_color_alpha(player);
    }

    if (local_palette) {
        if (!player->local_palette_warned) {
            ESP_LOGW(TAG, "GIF %s uses a local color table; running slower compatibility path", player->path);
            player->local_palette_warned = true;
        }
        player->stats.local_palette_frames++;
    }

    if (invalidate_area) {
        if (full_init || player->invalidate_full_frame) {
            invalidate_area->x1 = 0;
            invalidate_area->y1 = 0;
            invalidate_area->x2 = (lv_coord_t)player->render_w - 1;
            invalidate_area->y2 = (lv_coord_t)player->render_h - 1;
        } else {
            *invalidate_area = current_area;
            if (restore_previous) {
                *invalidate_area = ui_dual_gif_union_area(player->prev_frame_area, current_area);
            }
        }
    }

    player->prev_frame_area = current_area;
    player->prev_rect_valid = true;
    player->prev_disposal = frame_view.disposal;

    if (pixel_update_us) {
        *pixel_update_us = (uint64_t)esp_timer_get_time() - started;
    }

    return true;
}

static void ui_dual_gif_task(void *arg)
{
    ui_dual_gif_player_t *player = (ui_dual_gif_player_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();
    const bool is_bird_player = player == &s_bird_player;

    if (!player) {
        vTaskDelete(NULL);
        return;
    }

    while (!player->stop_requested) {
        uint32_t delay_ms = 0;
        uint16_t delay_cs = 0;
        uint64_t frame_started = 0;
        uint64_t gd_get_frame_started = 0;
        uint64_t gd_get_frame_us = 0;
        uint64_t pixel_update_us = 0;
        uint64_t lvgl_lock_us = 0;
        uint64_t presented_us = 0;
        int frame_result = 0;

        if (is_bird_player && !ui_dual_gif_apply_requested_bird_state(player, &last_wake)) {
            break;
        }

        if (is_bird_player && player->waiting_for_bird_state_change) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        delay_cs = ui_dual_gif_frame_delay_cs(player->gif);
        delay_ms = ui_dual_gif_frame_delay_ms(player->gif);
        ui_dual_gif_record_scheduled_delay(player, delay_cs, delay_ms);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(delay_ms));
        if (player->stop_requested) {
            break;
        }

        if (is_bird_player && !ui_dual_gif_apply_requested_bird_state(player, &last_wake)) {
            break;
        }

        if (is_bird_player && player->waiting_for_bird_state_change) {
            continue;
        }

        frame_started = (uint64_t)esp_timer_get_time();
        gd_get_frame_started = frame_started;

        frame_result = animation_decoder_decode_next(player->gif);
        if (frame_result == 0) {
            const bool bird_switch_pending = is_bird_player && player->bird_state_dirty;

            if (is_bird_player && !ui_dual_gif_apply_requested_bird_state(player, &last_wake)) {
                break;
            }

            if (bird_switch_pending) {
                continue;
            }

            const size_t next_index = player->sequence_index + 1U;

            if (next_index < player->sequence_count) {
                if (!ui_dual_gif_switch_to_sequence(player, next_index)) {
                    break;
                }

                last_wake = xTaskGetTickCount();
                continue;
            }

            if (!player->repeat || (is_bird_player && player->hold_last_frame_on_finish)) {
                ui_dual_gif_log_playback_finished(player, player->repeat ? "hold_last_frame" : "eof");
            }

            if (player == &s_book_player &&
                (s_book_action == UI_DUAL_GIF_BOOK_ACTION_OPEN ||
                 s_book_action == UI_DUAL_GIF_BOOK_ACTION_SWIPE) &&
                ui_dispatch_is_ready()) {
                s_book_transition_ready = true;
                (void)ui_dispatch_post(ui_dual_gif_try_notify_scene_image_ready,
                                       NULL,
                                       NULL,
                                       true,
                                       0);
            }

            if (player == &s_book_player && s_book_action == UI_DUAL_GIF_BOOK_ACTION_OPEN) {
                ESP_LOGI(TAG,
                         "release finished open cfa path=%s reason=prepare_first_swipe",
                         player->path ? player->path : "(null)");
                ui_dual_gif_close_gif_internal(player, false);
                (void)ui_dual_gif_preload_path(player, UI_DUAL_GIF_SWIPE_PATH);
            }

            if (player == &s_book_player && s_book_action == UI_DUAL_GIF_BOOK_ACTION_CLOSE) {
                ui_dual_gif_finish_bird_return_height();
                if (ui_dispatch_is_ready()) {
                    (void)ui_dispatch_post(ui_dual_gif_notify_close_finished_on_ui_thread,
                                           NULL,
                                           NULL,
                                           true,
                                           0);
                }
            }

            if (is_bird_player && player->hold_last_frame_on_finish) {
                player->waiting_for_bird_state_change = true;
                last_wake = xTaskGetTickCount();
                continue;
            }

            if (!player->repeat || !player->gif) {
                break;
            }

            (void)animation_decoder_rewind(player->gif);
            gd_get_frame_started = (uint64_t)esp_timer_get_time();
            frame_result = animation_decoder_decode_next(player->gif);
            gd_get_frame_us = (uint64_t)esp_timer_get_time() - gd_get_frame_started;
            if (frame_result <= 0) {
                ESP_LOGE(TAG, "unable to rewind GIF loop for %s, r=%d", player->path, frame_result);
                break;
            }
            ui_dual_gif_record_cfa_profile(player);

            if (!ui_dual_gif_present_frame(player, true, &pixel_update_us, &lvgl_lock_us)) {
                ESP_LOGE(TAG, "unable to render rewound GIF frame for %s", player->path);
                break;
            }

            presented_us = (uint64_t)esp_timer_get_time();
            ui_dual_gif_record_present_interval(player, presented_us);
            player->stats.frame_count++;
            ui_dual_gif_record_stat(&player->stats.gd_get_frame_us_total,
                                    &player->stats.gd_get_frame_us_max,
                                    gd_get_frame_us);
            ui_dual_gif_record_stat(&player->stats.pixel_update_us_total,
                                    &player->stats.pixel_update_us_max,
                                    pixel_update_us);
            ui_dual_gif_record_stat(&player->stats.lvgl_lock_us_total,
                                    &player->stats.lvgl_lock_us_max,
                                    lvgl_lock_us);

            ui_dual_gif_log_stats(player);
            last_wake = xTaskGetTickCount();
            continue;
        }
        gd_get_frame_us = (uint64_t)esp_timer_get_time() - gd_get_frame_started;

        if (frame_result <= 0) {
            ESP_LOGE(TAG, "unable to advance GIF frame for %s, r=%d", player->path, frame_result);
            break;
        }
        ui_dual_gif_record_cfa_profile(player);

        if (!ui_dual_gif_present_frame(player, false, &pixel_update_us, &lvgl_lock_us)) {
            ESP_LOGE(TAG, "unable to render GIF frame for %s", player->path);
            break;
        }

        presented_us = (uint64_t)esp_timer_get_time();
        ui_dual_gif_record_present_interval(player, presented_us);
        player->stats.frame_count++;
        ui_dual_gif_record_stat(&player->stats.gd_get_frame_us_total,
                                &player->stats.gd_get_frame_us_max,
                                gd_get_frame_us);
        ui_dual_gif_record_stat(&player->stats.pixel_update_us_total,
                                &player->stats.pixel_update_us_max,
                                pixel_update_us);
        ui_dual_gif_record_stat(&player->stats.lvgl_lock_us_total,
                                &player->stats.lvgl_lock_us_max,
                                lvgl_lock_us);

        if (((uint64_t)esp_timer_get_time() - frame_started) > ((uint64_t)delay_ms * 1000U)) {
            player->stats.missed_deadlines++;
            vTaskDelay(1);
        }

        ui_dual_gif_log_stats(player);
    }

    player->task = NULL;
    if (player->task_done_sem) {
        xSemaphoreGive(player->task_done_sem);
    }
    vTaskDelete(NULL);
}

static void ui_dual_gif_reset_runtime_state(ui_dual_gif_player_t *player)
{
    if (!player) {
        return;
    }

    player->img_buf_size = 0;
    player->render_buf_size = 0;
    player->pal565_source = NULL;
    player->restore_snapshot_valid = false;
    player->stop_requested = false;
    player->local_palette_warned = false;
    player->prev_rect_valid = false;
    player->prev_disposal = 0;
    player->src_px_count = 0;
    player->render_px_count = 0;
    player->preload_target_path = NULL;
    player->preload_target_index = 0;
    player->preload_generation = 0U;
    player->preload_enqueued = false;
    player->preloaded_path = NULL;
    player->preloaded_sequence_index = 0U;
    player->preloaded_file_size = 0U;
    player->preloaded_gif = NULL;
    player->bird_state_dirty = false;
    player->hold_last_frame_on_finish = false;
    player->waiting_for_bird_state_change = false;
    player->scheduled_delay_ms_pending = 0U;
    memset(&player->stats, 0, sizeof(player->stats));
    memset(&player->img_dsc, 0, sizeof(player->img_dsc));

    if (player == &s_bird_player) {
        player->requested_bird_state = (uint8_t)s_bird_state;
        player->active_bird_state = (uint8_t)s_bird_state;
    } else {
        player->requested_bird_state = 0U;
        player->active_bird_state = 0U;
    }
}

static void ui_dual_gif_release_asset(ui_dual_gif_player_t *player)
{
    uint8_t *img_buf = NULL;
    uint8_t *render_buf = NULL;
    const char *path = NULL;
    size_t file_size = 0U;

    if (!player) {
        return;
    }

    path = player->path;
    if (player->gif) {
        file_size = animation_decoder_file_size(player->gif);
        ui_dual_gif_store_cached_animation(path, player->gif, file_size);
        player->gif = NULL;
    }

    img_buf = player->img_buf;
    render_buf = player->render_buf;

    if (img_buf) {
        heap_caps_free(img_buf);
    }
    if (render_buf && render_buf != img_buf) {
        heap_caps_free(render_buf);
    }

    player->img_buf = NULL;
    player->render_buf = NULL;
    player->lv_buf = NULL;
    player->alpha_buf = NULL;
    player->img_buf_capacity = 0;
    player->render_buf_capacity = 0;
    ui_dual_gif_reset_runtime_state(player);
}

static void ui_dual_gif_close_gif_internal(ui_dual_gif_player_t *player, bool allow_cache)
{
    const char *path = NULL;
    size_t file_size = 0U;

    if (!player) {
        return;
    }

    path = player->path;
    if (player->gif) {
        file_size = animation_decoder_file_size(player->gif);
        if (allow_cache) {
            ui_dual_gif_store_cached_animation(path, player->gif, file_size);
        } else {
            ESP_LOGI(TAG,
                     "discard active book cfa path=%s size=%u reason=book_cfa_memory_reclaim",
                     path ? path : "(null)",
                     (unsigned)file_size);
            animation_decoder_close(player->gif);
        }
        player->gif = NULL;
    }

    player->pal565_source = NULL;
    player->local_palette_warned = false;
    player->prev_rect_valid = false;
    player->prev_disposal = 0;
    player->restore_snapshot_valid = false;
    player->src_px_count = 0;
    player->render_px_count = 0;
    memset(&player->stats, 0, sizeof(player->stats));
}

static void ui_dual_gif_close_gif(ui_dual_gif_player_t *player)
{
    ui_dual_gif_close_gif_internal(player, true);
}

static void ui_dual_gif_stop_player_task(ui_dual_gif_player_t *player, uint32_t wait_ms)
{
    TaskHandle_t task = NULL;
    SemaphoreHandle_t done_sem = NULL;

    if (!player) {
        return;
    }

    task = player->task;
    if (!task) {
        player->stop_requested = false;
        return;
    }

    done_sem = ui_dual_gif_ensure_done_sem(&player->task_done_sem);
    ui_dual_gif_drain_done_sem(done_sem);
    player->stop_requested = true;
    xTaskAbortDelay(task);
    ui_dual_gif_wait_for_signal("decoder task stop", player->path, &player->task, done_sem, wait_ms);

    player->stop_requested = false;
}

static void ui_dual_gif_free_player(ui_dual_gif_player_t *player)
{
    if (!player) {
        return;
    }

    ui_dual_gif_stop_player_task(player, UI_DUAL_GIF_TASK_STOP_WAIT_MS);

    ui_dual_gif_cancel_preload(player, UI_DUAL_GIF_PRELOAD_STOP_WAIT_MS);
    ui_dual_gif_clear_preloaded_file(player);
    ui_dual_gif_release_asset(player);
    player->entry_anim_started = false;
    player->source_attached = false;

    if (player->restore_color_buf) {
        heap_caps_free(player->restore_color_buf);
        player->restore_color_buf = NULL;
    }
    if (player->restore_alpha_buf) {
        heap_caps_free(player->restore_alpha_buf);
        player->restore_alpha_buf = NULL;
    }

    player->restore_buf_px_capacity = 0;

    if (player->sequence_paths && player->sequence_count > 0) {
        player->sequence_index = 0;
        player->path = player->sequence_paths[0];
    }
}

static void ui_dual_gif_prepare_player_reopen_for_path(ui_dual_gif_player_t *player, const char *next_path)
{
    bool reclaim_book_cfa_memory = false;
    bool keep_pending_preload = false;
    bool active_matches_next = false;
    const char *keep_preloaded_path = NULL;
    const char *keep_cached_path = NULL;

    if (!player) {
        return;
    }

    reclaim_book_cfa_memory = player == &s_book_player &&
                              (ui_dual_gif_path_is_cfa(player->path) ||
                               ui_dual_gif_path_is_cfa(player->preloaded_path));
    if (reclaim_book_cfa_memory &&
        ui_dual_gif_path_is_cfa(next_path)) {
        if (player->preloaded_path &&
            ui_dual_gif_paths_match(player->preloaded_path, next_path)) {
            keep_preloaded_path = next_path;
        }
        if (ui_dual_gif_has_cached_animation(next_path)) {
            keep_cached_path = next_path;
        }
        active_matches_next = player->gif &&
                              player->path &&
                              ui_dual_gif_paths_match(player->path, next_path);
        if (active_matches_next) {
            keep_cached_path = next_path;
        }
        keep_pending_preload = player->preload_enqueued &&
                               player->preload_target_path &&
                               ui_dual_gif_paths_match(player->preload_target_path, next_path);
        if (keep_preloaded_path || keep_pending_preload) {
            keep_cached_path = next_path;
        }
    }

    ui_dual_gif_stop_player_task(player, UI_DUAL_GIF_TASK_STOP_WAIT_MS);
    if (keep_pending_preload) {
        ESP_LOGI(TAG,
                 "keep pending book cfa preload path=%s reason=next_clip",
                 next_path ? next_path : "(null)");
    } else {
        ui_dual_gif_cancel_preload(player, UI_DUAL_GIF_PRELOAD_STOP_WAIT_MS);
    }
    ui_dual_gif_clear_preloaded_file_internal(player, !reclaim_book_cfa_memory, keep_cached_path);
    ui_dual_gif_close_gif_internal(player,
                                   !reclaim_book_cfa_memory || active_matches_next);
    if (reclaim_book_cfa_memory) {
        ui_dual_gif_purge_book_cfa_cache_except(keep_cached_path);
    }
}

static bool ui_dual_gif_matching_preload_ready(ui_dual_gif_player_t *player, const char *path)
{
    if (!player || !path) {
        return false;
    }

    if (player->preloaded_gif &&
        player->preloaded_path &&
        ui_dual_gif_paths_match(player->preloaded_path, path)) {
        return true;
    }

    return ui_dual_gif_has_cached_animation(path);
}

static bool ui_dual_gif_wait_for_matching_preload(ui_dual_gif_player_t *player, const char *path)
{
    TickType_t waited_ticks = 0;
    TickType_t poll_ticks = pdMS_TO_TICKS(UI_DUAL_GIF_CFA_PRELOAD_POLL_MS);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(UI_DUAL_GIF_CFA_PRELOAD_WAIT_MS);

    if (!player || !path) {
        return false;
    }

    if (ui_dual_gif_matching_preload_ready(player, path)) {
        return true;
    }

    if (!player->preload_enqueued ||
        !player->preload_target_path ||
        !ui_dual_gif_paths_match(player->preload_target_path, path)) {
        return false;
    }
    if (poll_ticks == 0) {
        poll_ticks = 1;
    }

    ESP_LOGI(TAG, "waiting for media preload path=%s", path);
    while (waited_ticks < timeout_ticks &&
           player->preload_enqueued &&
           player->preload_target_path &&
           ui_dual_gif_paths_match(player->preload_target_path, path)) {
        vTaskDelay(poll_ticks);
        waited_ticks += poll_ticks;

        if (ui_dual_gif_matching_preload_ready(player, path)) {
            ESP_LOGI(TAG,
                     "media preload ready path=%s waited_ms=%u",
                     path,
                     (unsigned)(waited_ticks * portTICK_PERIOD_MS));
            return true;
        }
    }

    if (ui_dual_gif_matching_preload_ready(player, path)) {
        ESP_LOGI(TAG,
                 "media preload ready path=%s waited_ms=%u",
                 path,
                 (unsigned)(waited_ticks * portTICK_PERIOD_MS));
        return true;
    }

    ESP_LOGW(TAG,
             "media preload wait timeout path=%s waited_ms=%u",
             path,
             (unsigned)(waited_ticks * portTICK_PERIOD_MS));
    return false;
}

static bool ui_dual_gif_require_book_cfa_preload(ui_dual_gif_player_t *player,
                                                 const char *path)
{
    uint32_t attempt = 0U;

    if (player != &s_book_player || !path || !ui_dual_gif_path_is_cfa(path)) {
        return false;
    }

    for (;;) {
        if (ui_dual_gif_matching_preload_ready(player, path)) {
            return true;
        }
        if (ui_dual_gif_wait_for_matching_preload(player, path)) {
            return true;
        }

        ui_dual_gif_cancel_preload(player, UI_DUAL_GIF_PRELOAD_STOP_WAIT_MS);
        ui_dual_gif_clear_preloaded_file_internal(player, false, path);
        ui_dual_gif_purge_book_cfa_cache_except(path);

        ++attempt;
        ESP_LOGW(TAG,
                 "required CFA preload retry path=%s attempt=%u free_psram=%u",
                 path,
                 (unsigned)attempt,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        if (!ui_dual_gif_preload_path(player, path)) {
            vTaskDelay(pdMS_TO_TICKS(UI_DUAL_GIF_CFA_PRELOAD_RETRY_MS));
            continue;
        }
        if (ui_dual_gif_wait_for_matching_preload(player, path)) {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(UI_DUAL_GIF_CFA_PRELOAD_RETRY_MS));
    }
}

static bool ui_dual_gif_open_player(ui_dual_gif_player_t *player)
{
    animation_decoder_canvas_info_t canvas_info = {0};
    int frame_result = 0;
    lv_area_t first_area = {0};
    uint64_t pixel_update_us = 0;
    size_t required_img_buf_size = 0;
    size_t required_render_buf_size = 0;
    const bool separate_render_buf = player && (player->use_true_color_alpha || !player->render_under_lvgl_lock);
    size_t cached_file_size = 0U;
    bool first_frame_ready = false;
    bool used_cached_gif = false;
    const bool render_live_under_lock = player && player->render_under_lvgl_lock && player->img_obj && player->source_attached;
    uint16_t first_delay_cs = 0U;
    uint32_t first_delay_ms = 0U;
    uint64_t first_present_us = 0U;
    const bool require_preloaded_book_cfa = player == &s_book_player &&
                                            player->path &&
                                            ui_dual_gif_path_is_cfa(player->path);

    if (!player) {
        return false;
    }

    if (player->gif) {
        return true;
    }

    if (require_preloaded_book_cfa) {
        (void)ui_dual_gif_require_book_cfa_preload(player, player->path);
    }

    if (player->preloaded_gif &&
        player->preloaded_path &&
        ui_dual_gif_paths_match(player->preloaded_path, player->path)) {
        player->gif = player->preloaded_gif;
        if (player->gif) {
            const size_t preloaded_file_size = player->preloaded_file_size;

            player->preloaded_gif = NULL;
            player->preloaded_file_size = 0;
            player->preloaded_path = NULL;
            player->preloaded_sequence_index = 0;
            first_frame_ready = animation_decoder_frame_ready(player->gif);

            UI_DIAG_LOGI("opened preloaded gif=%s seq=%u/%u size=%u",
                         player->path,
                         (unsigned)(player->sequence_index + 1U),
                         (unsigned)player->sequence_count,
                         (unsigned)preloaded_file_size);
        }
    }

    if (!player->gif) {
        player->gif = ui_dual_gif_take_cached_animation(player->path, &cached_file_size);
        if (player->gif) {
            used_cached_gif = true;
            UI_DIAG_LOGI("reused cached gif=%s seq=%u/%u size=%u",
                         player->path,
                         (unsigned)(player->sequence_index + 1U),
                         (unsigned)player->sequence_count,
                         (unsigned)cached_file_size);
        }
    }

    if (!player->gif) {
        if (require_preloaded_book_cfa) {
            ESP_LOGE(TAG, "required CFA preload missing path=%s", player->path);
            return false;
        }
        if (animation_decoder_open(player->path, ANIMATION_DECODER_KIND_AUTO, &player->gif) != ESP_OK) {
            player->gif = NULL;
        }
    }
    if (!player->gif) {
        ESP_LOGE(TAG, "animation_decoder_open(\"%s\") failed", player->path);
        return false;
    }

    frame_result = first_frame_ready ? 1 : animation_decoder_decode_next(player->gif);
    if (frame_result <= 0) {
        if (used_cached_gif) {
            animation_decoder_close(player->gif);
            player->gif = NULL;
            if (animation_decoder_open(player->path, ANIMATION_DECODER_KIND_AUTO, &player->gif) != ESP_OK) {
                player->gif = NULL;
            }
            used_cached_gif = false;
            if (player->gif) {
                frame_result = animation_decoder_decode_next(player->gif);
            }
        }
    }
    if (frame_result <= 0) {
        ESP_LOGE(TAG, "animation_decoder_decode_next(\"%s\") first frame failed, r=%d", player->path, frame_result);
        if (player->gif) {
            animation_decoder_close(player->gif);
            player->gif = NULL;
        }
        ui_dual_gif_free_player(player);
        return false;
    }

    if (animation_decoder_canvas_info(player->gif, &canvas_info) != ESP_OK) {
        ui_dual_gif_free_player(player);
        return false;
    }

    player->src_px_count = (uint32_t)canvas_info.width * (uint32_t)canvas_info.height;
    player->render_w = canvas_info.width;
    player->render_h = canvas_info.height;
    player->render_px_count = player->src_px_count;
    required_render_buf_size = (player->render_px_count * sizeof(lv_color_t)) + player->render_px_count;
    required_img_buf_size = player->use_true_color_alpha ?
                            (player->render_px_count * (sizeof(lv_color_t) + 1U)) :
                            required_render_buf_size;
    player->img_buf_size = required_img_buf_size;
    player->render_buf_size = required_render_buf_size;

    if (!player->img_buf || player->img_buf_capacity < required_img_buf_size) {
        if (player->img_buf) {
            heap_caps_free(player->img_buf);
            player->img_buf = NULL;
        }

        player->img_buf = heap_caps_malloc(required_img_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!player->img_buf) {
            player->img_buf = heap_caps_malloc(required_img_buf_size, MALLOC_CAP_8BIT);
        }
        if (!player->img_buf) {
            ESP_LOGE(TAG, "GIF buffer allocation failed for %s", player->path);
            ui_dual_gif_free_player(player);
            return false;
        }

        player->img_buf_capacity = required_img_buf_size;
    }

    if (separate_render_buf) {
        if (!player->render_buf || player->render_buf_capacity < required_render_buf_size) {
            if (player->render_buf) {
                heap_caps_free(player->render_buf);
                player->render_buf = NULL;
            }

            player->render_buf = heap_caps_malloc(required_render_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!player->render_buf) {
                player->render_buf = heap_caps_malloc(required_render_buf_size, MALLOC_CAP_8BIT);
            }
            if (!player->render_buf) {
                ESP_LOGE(TAG, "GIF render buffer allocation failed for %s", player->path);
                ui_dual_gif_free_player(player);
                return false;
            }

            player->render_buf_capacity = required_render_buf_size;
        }
    } else {
        player->render_buf = player->img_buf;
        player->render_buf_capacity = player->img_buf_capacity;
    }

    player->lv_buf = (lv_color_t *)player->render_buf;
    player->alpha_buf = player->render_buf + (player->render_px_count * sizeof(lv_color_t));

    if (render_live_under_lock) {
        if (!lvgl_port_lock(1000)) {
            ESP_LOGW(TAG, "LVGL lock timed out while preparing first frame for %s", player->path);
            ui_dual_gif_free_player(player);
            return false;
        }

        if (!ui_dual_gif_render_current_frame(player, &first_area, &pixel_update_us, true)) {
            lvgl_port_unlock();
            ESP_LOGE(TAG, "first frame render failed for %s", player->path);
            ui_dual_gif_free_player(player);
            return false;
        }

        lvgl_port_unlock();
    } else if (!ui_dual_gif_render_current_frame(player, &first_area, &pixel_update_us, true)) {
        ESP_LOGE(TAG, "first frame render failed for %s", player->path);
        ui_dual_gif_free_player(player);
        return false;
    }

    player->img_dsc.header.always_zero = 0;
    player->img_dsc.header.w = player->render_w;
    player->img_dsc.header.h = player->render_h;
    player->img_dsc.header.cf = player->use_true_color_alpha ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_RGB565A8;
    player->img_dsc.data = (const uint8_t *)player->img_buf;
    player->img_dsc.data_size = player->img_buf_size;

    first_delay_cs = ui_dual_gif_frame_delay_cs(player->gif);
    first_delay_ms = ui_dual_gif_frame_delay_ms(player->gif);
    first_present_us = (uint64_t)esp_timer_get_time();
    ui_dual_gif_reset_stats(player, first_present_us);
    ui_dual_gif_record_cfa_profile(player);
    ui_dual_gif_record_stat(&player->stats.pixel_update_us_total,
                            &player->stats.pixel_update_us_max,
                            pixel_update_us);

    UI_DIAG_LOGI("opened gif=%s seq=%u/%u size=%ux%u first_delay_cs=%u first_delay_ms=%u "
                 "img_buf=%p img_buf_size=%u capacity=%u free_psram=%u",
                 player->path,
                 (unsigned)(player->sequence_index + 1U),
                 (unsigned)player->sequence_count,
                 (unsigned)player->render_w,
                 (unsigned)player->render_h,
                 (unsigned)first_delay_cs,
                 (unsigned)first_delay_ms,
                 (void *)player->img_buf,
                 (unsigned)player->img_buf_size,
                 (unsigned)player->img_buf_capacity,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    return true;
}

static void ui_dual_gif_attach_source(ui_dual_gif_player_t *player)
{
    if (!player || !player->img_obj) {
        return;
    }

    lv_obj_invalidate(player->img_obj);
    lv_img_set_src(player->img_obj, &player->img_dsc);
    lv_obj_align(player->img_obj, player->align, player->align_x, player->align_y);
    if (player == &s_book_player) {
        ui_dual_gif_align_story_layers();
    }
    player->source_attached = true;
    if (player->animate_entry) {
        if (!player->entry_anim_started) {
            lv_obj_set_style_translate_x(player->img_obj, player->entry_start_shift_x, LV_PART_MAIN);
            lv_obj_set_style_translate_y(player->img_obj, player->entry_start_shift_y, LV_PART_MAIN);
        }
    } else {
        lv_obj_set_style_translate_x(player->img_obj, 0, LV_PART_MAIN);
        lv_obj_set_style_translate_y(player->img_obj, 0, LV_PART_MAIN);
    }
    lv_obj_invalidate(player->img_obj);

    if (player == &s_book_player) {
        if (s_book_action == UI_DUAL_GIF_BOOK_ACTION_OPEN) {
            ui_dual_gif_mark_book_animation_started();
        } else if (s_book_action == UI_DUAL_GIF_BOOK_ACTION_SWIPE) {
            ui_dual_gif_start_overlay_stage_sequence(s_overlay_story_img,
                                                     UI_DUAL_GIF_SWIPE_OVERLAY_SHOW_DELAY_MS,
                                                     UI_DUAL_GIF_SWIPE_OVERLAY_HIDE_DELAY_MS,
                                                     true,
                                                     true);
        } else if (s_book_action == UI_DUAL_GIF_BOOK_ACTION_CLOSE) {
            ui_dual_gif_hide_overlay();
            ui_dual_gif_start_bird_return_animation();
        }
    } else if (player == &s_bird_player) {
        ui_dual_gif_try_start_bird_entry_animation();
    }
}

static bool ui_dual_gif_switch_to_sequence(ui_dual_gif_player_t *player, size_t next_index)
{
    const lv_img_dsc_t *overlay_story_img = NULL;
    uint32_t overlay_show_delay_ms = 0U;
    uint32_t overlay_hide_delay_ms = 0U;
    bool start_overlay = false;
    bool hide_overlay = false;

    if (!player || !player->sequence_paths || next_index >= player->sequence_count) {
        return false;
    }

    UI_DIAG_LOGI("switching gif seq %u/%u: %s -> %s",
                 (unsigned)(player->sequence_index + 1U),
                 (unsigned)player->sequence_count,
                 player->path,
                 player->sequence_paths[next_index]);

    ui_dual_gif_close_gif(player);

    player->sequence_index = next_index;
    player->path = player->sequence_paths[next_index];

    if (player == &s_book_player && s_book_action == UI_DUAL_GIF_BOOK_ACTION_SWIPE) {
        start_overlay = true;
        overlay_story_img = s_overlay_story_img;
        overlay_show_delay_ms = UI_DUAL_GIF_SWIPE_OVERLAY_SHOW_DELAY_MS;
        overlay_hide_delay_ms = UI_DUAL_GIF_SWIPE_OVERLAY_HIDE_DELAY_MS;
    } else if (player == &s_book_player && s_book_action == UI_DUAL_GIF_BOOK_ACTION_CLOSE) {
        hide_overlay = true;
    }

    if (!ui_dual_gif_open_player(player)) {
        ESP_LOGE(TAG, "unable to open next GIF in sequence: %s", player->path);
        return false;
    }

    if (player->img_obj) {
        lv_area_t full_area = {
            .x1 = 0,
            .y1 = 0,
            .x2 = (lv_coord_t)player->render_w - 1,
            .y2 = (lv_coord_t)player->render_h - 1,
        };

        if (!lvgl_port_lock(1000)) {
            ESP_LOGW(TAG, "LVGL lock timed out while updating %s", player->path);
            return false;
        }

        ui_dual_gif_sync_rgb565a8_area(player, &full_area);
        lv_img_cache_invalidate_src(&player->img_dsc);
        ui_dual_gif_attach_source(player);
        if (start_overlay) {
            ui_dual_gif_start_overlay_stage_sequence(overlay_story_img,
                                                     overlay_show_delay_ms,
                                                     overlay_hide_delay_ms,
                                                     true,
                                                     true);
        }
        if (hide_overlay) {
            ui_dual_gif_hide_overlay();
        }
        lvgl_port_unlock();
    }

    ui_dual_gif_schedule_next_preload(player);
    return true;
}

static bool ui_dual_gif_start_player_task(ui_dual_gif_player_t *player)
{
    BaseType_t task_result = pdFAIL;

    if (!player || player->task) {
        return player != NULL;
    }

    task_result = xTaskCreatePinnedToCore(ui_dual_gif_task,
                                          "gif_decode",
                                          UI_DUAL_GIF_TASK_STACK,
                                          player,
                                          player->task_priority ? player->task_priority : UI_DUAL_GIF_TASK_PRIORITY,
                                          &player->task,
                                          UI_DUAL_GIF_TASK_CORE);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "unable to create decoder task for %s", player->path);
        player->task = NULL;
        return false;
    }

    return true;
}

static bool ui_dual_gif_attach_current_frame(ui_dual_gif_player_t *player)
{
    if (!player || !player->img_obj) {
        return false;
    }

    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "LVGL lock timed out while attaching %s", player->path);
        return false;
    }

    if (player->img_buf != player->render_buf && !player->use_true_color_alpha) {
        const lv_area_t full_area = {
            .x1 = 0,
            .y1 = 0,
            .x2 = (lv_coord_t)player->render_w - 1,
            .y2 = (lv_coord_t)player->render_h - 1,
        };

        ui_dual_gif_sync_rgb565a8_area(player, &full_area);
    }

    lv_img_cache_invalidate_src(&player->img_dsc);
    ui_dual_gif_attach_source(player);
    lvgl_port_unlock();
    return true;
}

static bool ui_dual_gif_attach_current_frame_on_ui_thread(ui_dual_gif_player_t *player)
{
    if (!player || !player->img_obj) {
        return false;
    }

    if (player->img_buf != player->render_buf && !player->use_true_color_alpha) {
        const lv_area_t full_area = {
            .x1 = 0,
            .y1 = 0,
            .x2 = (lv_coord_t)player->render_w - 1,
            .y2 = (lv_coord_t)player->render_h - 1,
        };

        ui_dual_gif_sync_rgb565a8_area(player, &full_area);
    }

    lv_img_cache_invalidate_src(&player->img_dsc);
    ui_dual_gif_attach_source(player);
    return true;
}

static bool ui_dual_gif_request_bird_state(ui_dual_gif_bird_state_t state)
{
    ui_dual_gif_player_t *player = &s_bird_player;
    const ui_dual_gif_bird_state_cfg_t *cfg = ui_dual_gif_get_bird_state_cfg(state);

    if (!cfg) {
        ESP_LOGW(TAG, "unsupported bird state=%u", (unsigned)state);
        return false;
    }

    if (s_bird_state == state &&
        !player->bird_state_dirty &&
        player->sequence_paths == cfg->sequence_paths &&
        player->sequence_count == cfg->sequence_count) {
        return true;
    }

    player->requested_bird_state = (uint8_t)state;
    player->bird_state_dirty = true;
    player->waiting_for_bird_state_change = false;
    if (player->task) {
        xTaskAbortDelay(player->task);
    }
    return true;
}

static bool ui_dual_gif_apply_requested_bird_state(ui_dual_gif_player_t *player, TickType_t *last_wake)
{
    const ui_dual_gif_bird_state_t requested_state = player ?
                                                     (ui_dual_gif_bird_state_t)player->requested_bird_state :
                                                     UI_DUAL_GIF_BIRD_STATE_IDLE;

    if (!player || player != &s_bird_player || !player->bird_state_dirty) {
        return true;
    }

    ui_dual_gif_close_gif(player);
    ui_dual_gif_apply_bird_state_config(player, requested_state);

    if (!ui_dual_gif_open_player(player)) {
        ESP_LOGE(TAG, "unable to open bird state=%u", (unsigned)requested_state);
        return false;
    }

    if (requested_state == UI_DUAL_GIF_BIRD_STATE_PRE_TALK) {
        (void)ui_dual_gif_preload_path(player, UI_DUAL_GIF_BIRD_LOOP_PATH);
    }

    if (player->img_obj && !ui_dual_gif_attach_current_frame(player)) {
        return false;
    }

    s_bird_state = requested_state;
    player->active_bird_state = (uint8_t)requested_state;
    player->requested_bird_state = (uint8_t)requested_state;
    player->bird_state_dirty = false;

    if (last_wake) {
        *last_wake = xTaskGetTickCount();
    }

    return true;
}

static void ui_dual_gif_stop_bird_timer(void)
{
    if (s_bird_frame_timer) {
        lv_timer_pause(s_bird_frame_timer);
    }
    s_bird_player.scheduled_delay_ms_pending = 0U;
}

static void ui_dual_gif_arm_bird_timer(uint32_t delay_ms)
{
    if (delay_ms == 0U) {
        delay_ms = 1U;
    }

    if (!s_bird_frame_timer) {
        s_bird_frame_timer = lv_timer_create(ui_dual_gif_bird_timer_cb, delay_ms, NULL);
        if (!s_bird_frame_timer) {
            ESP_LOGW(TAG, "unable to create bird frame timer");
            return;
        }
    }

    lv_timer_set_period(s_bird_frame_timer, delay_ms);
    lv_timer_reset(s_bird_frame_timer);
    lv_timer_resume(s_bird_frame_timer);
    s_bird_player.scheduled_delay_ms_pending = delay_ms;
}

static void ui_dual_gif_bird_timer_cb(lv_timer_t *timer)
{
    ui_dual_gif_player_t *player = &s_bird_player;
    uint16_t delay_cs = 0U;
    uint32_t delay_ms = 0U;
    uint64_t gd_get_frame_started = 0;
    uint64_t gd_get_frame_us = 0;
    uint64_t pixel_update_us = 0;
    uint64_t presented_us = 0;
    int frame_result = 0;

    if (!timer || !player->gif) {
        ui_dual_gif_stop_bird_timer();
        return;
    }

    if (player->scheduled_delay_ms_pending > 0U) {
        delay_cs = ui_dual_gif_frame_delay_cs(player->gif);
        ui_dual_gif_record_scheduled_delay(player, delay_cs, player->scheduled_delay_ms_pending);
        player->scheduled_delay_ms_pending = 0U;
    }

    if (player->bird_state_dirty) {
        if (!ui_dual_gif_set_bird_state_now((ui_dual_gif_bird_state_t)player->requested_bird_state)) {
            ESP_LOGW(TAG, "bird pending state apply failed");
            ui_dual_gif_stop_bird_timer();
        }
        return;
    }

    if (player->waiting_for_bird_state_change) {
        lv_timer_set_period(timer, 20U);
        return;
    }

    gd_get_frame_started = (uint64_t)esp_timer_get_time();
    frame_result = animation_decoder_decode_next(player->gif);
    gd_get_frame_us = (uint64_t)esp_timer_get_time() - gd_get_frame_started;

    if (frame_result == 0) {
        if (player->hold_last_frame_on_finish) {
            ui_dual_gif_log_playback_finished(player, "hold_last_frame");
            player->waiting_for_bird_state_change = true;
            lv_timer_set_period(timer, 20U);
            return;
        }

        if (!player->repeat) {
            ui_dual_gif_log_playback_finished(player, "eof");
            ui_dual_gif_stop_bird_timer();
            return;
        }

        (void)animation_decoder_rewind(player->gif);
        gd_get_frame_started = (uint64_t)esp_timer_get_time();
        frame_result = animation_decoder_decode_next(player->gif);
        gd_get_frame_us = (uint64_t)esp_timer_get_time() - gd_get_frame_started;
    }

    if (frame_result <= 0) {
        ESP_LOGW(TAG, "bird timer failed for %s, r=%d", player->path ? player->path : "<null>", frame_result);
        ui_dual_gif_stop_bird_timer();
        return;
    }
    ui_dual_gif_record_cfa_profile(player);

    ui_dual_gif_record_stat(&player->stats.gd_get_frame_us_total,
                            &player->stats.gd_get_frame_us_max,
                            gd_get_frame_us);

    if (!ui_dual_gif_present_frame_on_ui_thread(player, false, &pixel_update_us)) {
        ESP_LOGW(TAG, "bird present failed for %s", player->path ? player->path : "<null>");
        ui_dual_gif_stop_bird_timer();
        return;
    }

    presented_us = (uint64_t)esp_timer_get_time();
    ui_dual_gif_record_present_interval(player, presented_us);
    player->stats.frame_count++;
    ui_dual_gif_record_stat(&player->stats.pixel_update_us_total,
                            &player->stats.pixel_update_us_max,
                            pixel_update_us);
    ui_dual_gif_log_stats(player);
    delay_ms = ui_dual_gif_frame_delay_ms(player->gif);
    player->scheduled_delay_ms_pending = delay_ms;
    lv_timer_set_period(timer, delay_ms);
}

static bool ui_dual_gif_set_bird_state_now(ui_dual_gif_bird_state_t state)
{
    ui_dual_gif_player_t *player = &s_bird_player;
    const ui_dual_gif_bird_state_cfg_t *cfg = ui_dual_gif_get_bird_state_cfg(state);

    if (!cfg) {
        ESP_LOGW(TAG, "unsupported bird state=%u", (unsigned)state);
        return false;
    }

    if (s_bird_state == state && player->gif && !player->waiting_for_bird_state_change) {
        return true;
    }

    UI_DIAG_LOGI("bird_timing stage=bird_state_apply ts_us=%llu state=%u prev_state=%u waiting=%u path=%s",
                 (unsigned long long)esp_timer_get_time(),
                 (unsigned)state,
                 (unsigned)s_bird_state,
                 player->waiting_for_bird_state_change ? 1U : 0U,
                 cfg->path ? cfg->path : "<null>");

    ui_dual_gif_stop_bird_timer();
    player->waiting_for_bird_state_change = false;

    if (state == UI_DUAL_GIF_BIRD_STATE_PRE_TALK) {
        (void)ui_dual_gif_preload_path(player, UI_DUAL_GIF_BIRD_LOOP_PATH);
    }

    ui_dual_gif_close_gif(player);
    ui_dual_gif_apply_bird_state_config(player, state);

    if (!ui_dual_gif_open_player(player)) {
        ESP_LOGE(TAG, "unable to open bird state=%u", (unsigned)state);
        return false;
    }

    if (!ui_dual_gif_attach_current_frame_on_ui_thread(player)) {
        ESP_LOGE(TAG, "unable to attach bird state=%u", (unsigned)state);
        return false;
    }

    s_bird_state = state;
    player->active_bird_state = (uint8_t)state;
    player->requested_bird_state = (uint8_t)state;
    player->bird_state_dirty = false;

    ui_dual_gif_arm_bird_timer(ui_dual_gif_frame_delay_ms(player->gif));
    return true;
}

static void ui_dual_gif_start_player(ui_dual_gif_player_t *player)
{
    if (!player || !player->img_obj) {
        return;
    }

    if (!ui_dual_gif_open_player(player)) {
        return;
    }

    if (!ui_dual_gif_attach_current_frame(player)) {
        return;
    }

    if (!ui_dual_gif_start_player_task(player)) {
        return;
    }

    ui_dual_gif_schedule_next_preload(player);
}

static void ui_dual_gif_control_task(void *arg)
{
    (void)arg;

    for (;;) {
        ui_dual_gif_ctrl_cmd_t cmd = {0};

        if (xQueueReceive(s_control_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (cmd.type) {
        case UI_DUAL_GIF_CTRL_CMD_RESET_STORY:
            ui_dual_gif_reset_story_state_now();
            break;

        case UI_DUAL_GIF_CTRL_CMD_START_OPEN:
            ui_dual_gif_start_open_now();
            break;

        case UI_DUAL_GIF_CTRL_CMD_START_SWIPE:
            ui_dual_gif_start_swipe_now();
            break;

        case UI_DUAL_GIF_CTRL_CMD_START_CLOSE:
            ui_dual_gif_start_close_now();
            break;

        case UI_DUAL_GIF_CTRL_CMD_SET_BIRD_MODE:
            (void)ui_dual_gif_request_bird_state((ui_dual_gif_bird_state_t)cmd.value);
            break;
        }
    }
}

static bool ui_dual_gif_control_init(void)
{
    if (s_control_queue && s_control_task) {
        return true;
    }

    if (!s_control_queue) {
        s_control_queue = xQueueCreate(8, sizeof(ui_dual_gif_ctrl_cmd_t));
        if (!s_control_queue) {
            ESP_LOGE(TAG, "unable to create control queue");
            return false;
        }
    }

    if (!s_control_task) {
        if (xTaskCreatePinnedToCore(ui_dual_gif_control_task,
                                    "gif_control",
                                    UI_DUAL_GIF_CONTROL_TASK_STACK,
                                    NULL,
                                    UI_DUAL_GIF_CONTROL_TASK_PRIORITY,
                                    &s_control_task,
                                    UI_DUAL_GIF_CONTROL_TASK_CORE) != pdPASS) {
            ESP_LOGE(TAG, "unable to create control task");
            vQueueDelete(s_control_queue);
            s_control_queue = NULL;
            return false;
        }
    }

    return true;
}

static void ui_dual_gif_ensure_animation_cache(void)
{
    if (s_animation_cache.lock) {
        return;
    }

    if (media_cache_init(&s_animation_cache,
                         s_animation_cache_slots,
                         UI_DUAL_GIF_ARRAY_SIZE(s_animation_cache_slots),
                         ui_dual_gif_release_cached_animation_value) != ESP_OK) {
        ESP_LOGW(TAG, "unable to initialize animation cache");
    }
}

static bool ui_dual_gif_post_control_command(ui_dual_gif_ctrl_cmd_type_t type, uint8_t value)
{
    ui_dual_gif_ctrl_cmd_t cmd = {
        .type = type,
        .value = value,
    };

    if (!ui_dual_gif_control_init()) {
        return false;
    }

    if (xQueueSend(s_control_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "unable to queue control command=%d", (int)type);
        return false;
    }

    return true;
}

static void ui_dual_gif_configure_book_action(ui_dual_gif_book_action_t action)
{
    s_book_action = action;
    s_book_player.sequence_index = 0U;
    s_book_player.repeat = false;

    switch (action) {
    case UI_DUAL_GIF_BOOK_ACTION_OPEN:
        s_book_player.path = UI_DUAL_GIF_OPEN_PATH;
        s_book_player.sequence_paths = s_book_open_sequence_paths;
        s_book_player.sequence_count = sizeof(s_book_open_sequence_paths) / sizeof(s_book_open_sequence_paths[0]);
        break;
    case UI_DUAL_GIF_BOOK_ACTION_SWIPE:
        s_book_player.path = UI_DUAL_GIF_SWIPE_PATH;
        s_book_player.sequence_paths = s_book_swipe_sequence_paths;
        s_book_player.sequence_count = sizeof(s_book_swipe_sequence_paths) / sizeof(s_book_swipe_sequence_paths[0]);
        break;
    case UI_DUAL_GIF_BOOK_ACTION_CLOSE:
        s_book_player.path = UI_DUAL_GIF_CLOSE_PATH;
        s_book_player.sequence_paths = s_book_close_sequence_paths;
        s_book_player.sequence_count = sizeof(s_book_close_sequence_paths) / sizeof(s_book_close_sequence_paths[0]);
        break;
    case UI_DUAL_GIF_BOOK_ACTION_NONE:
    default:
        s_book_player.path = UI_DUAL_GIF_OPEN_PATH;
        s_book_player.sequence_paths = s_book_open_sequence_paths;
        s_book_player.sequence_count = sizeof(s_book_open_sequence_paths) / sizeof(s_book_open_sequence_paths[0]);
        break;
    }
}

static void ui_dual_gif_prepare_screen(void)
{
    if (s_dual_gif_screen) {
        return;
    }

    (void)ui_dual_gif_control_init();
    ui_dual_gif_ensure_animation_cache();

    s_dual_gif_screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(s_dual_gif_screen, ui_dual_gif_screen_delete_event_cb, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(s_dual_gif_screen);
    lv_obj_clear_flag(s_dual_gif_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_dual_gif_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dual_gif_screen, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(s_dual_gif_screen, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(s_dual_gif_screen, LV_GRAD_DIR_HOR, LV_PART_MAIN);

    ui_dual_gif_create_background_decor();

    s_story_img_button = lv_imgbtn_create(s_dual_gif_screen);
    lv_imgbtn_set_src(s_story_img_button, LV_IMGBTN_STATE_RELEASED, NULL, &ui_img_sett_on_png, NULL);
    lv_imgbtn_set_src(s_story_img_button, LV_IMGBTN_STATE_PRESSED, NULL, &ui_img_sett_on_png, NULL);
    lv_obj_set_size(s_story_img_button, 64, 64);
    lv_obj_set_x(s_story_img_button, -477);
    lv_obj_set_y(s_story_img_button, -265);
    lv_obj_set_align(s_story_img_button, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_story_img_button, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_story_img_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_story_img_button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_story_img_button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_story_img_button, 0, LV_PART_MAIN);
    lv_obj_set_style_opa(s_story_img_button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(s_story_img_button, ui_dual_gif_settings_open_event_cb, LV_EVENT_CLICKED, NULL);

    s_bird_img = lv_img_create(s_dual_gif_screen);
    s_bird_player.img_obj = s_bird_img;

    s_book_img = lv_img_create(s_dual_gif_screen);
    s_book_player.img_obj = s_book_img;

    s_book_overlay_layer = lv_obj_create(s_dual_gif_screen);
    lv_obj_remove_style_all(s_book_overlay_layer);
    lv_obj_clear_flag(s_book_overlay_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_book_overlay_layer, UI_DUAL_GIF_BOOK_OVERLAY_WIDTH, UI_DUAL_GIF_BOOK_OVERLAY_HEIGHT);
    ui_dual_gif_align_story_layers();
    lv_obj_add_flag(s_book_overlay_layer, LV_OBJ_FLAG_HIDDEN);

    s_a_small_img = lv_img_create(s_book_overlay_layer);
    lv_img_set_src(s_a_small_img, ui_dual_gif_get_default_overlay_image());
    lv_obj_align(s_a_small_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_img_opa(s_a_small_img, LV_OPA_TRANSP, LV_PART_MAIN);

    s_story_text_view = lv_obj_create(s_book_img);
    lv_obj_remove_style_all(s_story_text_view);
    lv_obj_set_size(s_story_text_view, UI_DUAL_GIF_STORY_TEXT_WIDTH, UI_DUAL_GIF_STORY_TEXT_HEIGHT);
    lv_obj_set_x(s_story_text_view, UI_DUAL_GIF_STORY_TEXT_OFFSET_X);
    lv_obj_set_y(s_story_text_view, UI_DUAL_GIF_STORY_TEXT_OFFSET_Y);
    lv_obj_set_align(s_story_text_view, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_story_text_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_story_text_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_story_text_view, LV_DIR_VER);
    lv_obj_set_scroll_snap_x(s_story_text_view, LV_SCROLL_SNAP_NONE);
    lv_obj_set_scroll_snap_y(s_story_text_view, LV_SCROLL_SNAP_NONE);
    lv_obj_add_flag(s_story_text_view, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(s_story_text_view, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_style_anim_time(s_story_text_view, UI_DUAL_GIF_STORY_TEXT_SCROLL_ANIM_MS, LV_PART_MAIN);
    lv_obj_set_style_opa(s_story_text_view, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_story_text_view, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_story_text_view, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_story_text_view, LV_OBJ_FLAG_HIDDEN);

    s_story_text_label = lv_label_create(s_story_text_view);
    lv_label_set_long_mode(s_story_text_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_story_text_label, "");
    lv_obj_set_width(s_story_text_label, UI_DUAL_GIF_STORY_TEXT_WIDTH - (2 * UI_DUAL_GIF_STORY_TEXT_PAD_X));
    lv_obj_set_style_text_align(s_story_text_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_story_text_label, lv_color_hex(UI_DUAL_GIF_STORY_TEXT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_story_text_label, &ui_font_Font11, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_story_text_label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_story_text_label, 0, LV_PART_MAIN);
    ui_dual_gif_layout_story_text();

    s_story_choice_container = lv_obj_create(s_book_img);
    lv_obj_remove_style_all(s_story_choice_container);
    lv_obj_set_width(s_story_choice_container, UI_DUAL_GIF_STORY_CHOICE_WIDTH);
    lv_obj_set_height(s_story_choice_container, UI_DUAL_GIF_STORY_CHOICE_HEIGHT);
    lv_obj_set_x(s_story_choice_container, UI_DUAL_GIF_STORY_CHOICE_OFFSET_X);
    lv_obj_set_y(s_story_choice_container, UI_DUAL_GIF_STORY_CHOICE_OFFSET_Y);
    lv_obj_set_align(s_story_choice_container, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(s_story_choice_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_story_choice_container,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_story_choice_container, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_left(s_story_choice_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(s_story_choice_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(s_story_choice_container, UI_DUAL_GIF_STORY_CHOICE_TOP_PAD, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(s_story_choice_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(s_story_choice_container, UI_DUAL_GIF_STORY_CHOICE_ROW_PAD, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_opa(s_story_choice_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_story_choice_container, LV_OBJ_FLAG_HIDDEN);

    s_story_choice_question_panel = lv_obj_create(s_story_choice_container);
    lv_obj_set_width(s_story_choice_question_panel, 358);
    lv_obj_set_height(s_story_choice_question_panel, LV_SIZE_CONTENT);
    lv_obj_set_x(s_story_choice_question_panel, 212);
    lv_obj_set_y(s_story_choice_question_panel, -67);
    lv_obj_set_align(s_story_choice_question_panel, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(s_story_choice_question_panel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_story_choice_question_panel,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_story_choice_question_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_story_choice_question_panel, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_story_choice_question_panel, lv_color_hex(0xEBDA8B), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_story_choice_question_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_story_choice_question_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(s_story_choice_question_panel, lv_color_hex(0x3F0906), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(s_story_choice_question_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_story_choice_question_label = lv_label_create(s_story_choice_question_panel);
    lv_obj_set_width(s_story_choice_question_label, 356);
    lv_obj_set_height(s_story_choice_question_label, 34);
    lv_obj_set_x(s_story_choice_question_label, 60);
    lv_obj_set_y(s_story_choice_question_label, 211);
    lv_obj_set_align(s_story_choice_question_label, LV_ALIGN_CENTER);
    lv_label_set_text(s_story_choice_question_label, "");
    lv_obj_set_style_text_color(s_story_choice_question_label, lv_color_hex(0x490909), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(s_story_choice_question_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_story_choice_question_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_story_choice_question_label, &ui_font_Font22, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_story_choice_options_container = lv_obj_create(s_story_choice_container);
    lv_obj_remove_style_all(s_story_choice_options_container);
    lv_obj_set_width(s_story_choice_options_container, UI_DUAL_GIF_STORY_CHOICE_OPTIONS_WIDTH);
    lv_obj_set_height(s_story_choice_options_container, UI_DUAL_GIF_STORY_CHOICE_OPTIONS_HEIGHT);
    lv_obj_set_x(s_story_choice_options_container, UI_DUAL_GIF_STORY_CHOICE_OPTIONS_OFFSET_X);
    lv_obj_set_y(s_story_choice_options_container, UI_DUAL_GIF_STORY_CHOICE_OPTIONS_OFFSET_Y);
    lv_obj_set_align(s_story_choice_options_container, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(s_story_choice_options_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_story_choice_options_container,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_story_choice_options_container, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(s_story_choice_options_container, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(s_story_choice_options_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_story_choice_button1 = lv_btn_create(s_story_choice_options_container);
    lv_obj_set_width(s_story_choice_button1, UI_DUAL_GIF_STORY_CHOICE_BUTTON_WIDTH);
    lv_obj_set_height(s_story_choice_button1, UI_DUAL_GIF_STORY_CHOICE_BUTTON_HEIGHT);
    lv_obj_set_x(s_story_choice_button1, 208);
    lv_obj_set_y(s_story_choice_button1, 55);
    lv_obj_set_align(s_story_choice_button1, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(s_story_choice_button1, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_story_choice_button1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_story_choice_button1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_story_choice_button1, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_story_choice_button1, lv_color_hex(0xEBDA8B), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_story_choice_button1, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_story_choice_button1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(s_story_choice_button1, lv_color_hex(0x550601), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(s_story_choice_button1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(s_story_choice_button1, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(s_story_choice_button1, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_story_choice_button1, lv_color_hex(0xE1A507), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(s_story_choice_button1, 70, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_story_choice_button1, ui_dual_gif_story_choice_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    s_story_choice_button1_label = lv_label_create(s_story_choice_button1);
    lv_obj_set_width(s_story_choice_button1_label, 329);
    lv_obj_set_height(s_story_choice_button1_label, LV_SIZE_CONTENT);
    lv_obj_set_x(s_story_choice_button1_label, 60);
    lv_obj_set_y(s_story_choice_button1_label, 211);
    lv_obj_set_align(s_story_choice_button1_label, LV_ALIGN_CENTER);
    lv_label_set_text(s_story_choice_button1_label, "");
    lv_obj_set_style_text_color(s_story_choice_button1_label, lv_color_hex(0x490909), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(s_story_choice_button1_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_story_choice_button1_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_story_choice_button1_label, &ui_font_Font33, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_story_choice_button2 = lv_btn_create(s_story_choice_options_container);
    lv_obj_set_width(s_story_choice_button2, UI_DUAL_GIF_STORY_CHOICE_BUTTON_WIDTH);
    lv_obj_set_height(s_story_choice_button2, UI_DUAL_GIF_STORY_CHOICE_BUTTON_HEIGHT);
    lv_obj_set_x(s_story_choice_button2, 213);
    lv_obj_set_y(s_story_choice_button2, 189);
    lv_obj_set_align(s_story_choice_button2, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(s_story_choice_button2, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_story_choice_button2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_story_choice_button2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_story_choice_button2, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_story_choice_button2, lv_color_hex(0xEBDA8B), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_story_choice_button2, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_story_choice_button2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(s_story_choice_button2, lv_color_hex(0x550601), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(s_story_choice_button2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(s_story_choice_button2, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(s_story_choice_button2, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_story_choice_button2, lv_color_hex(0xE1A507), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(s_story_choice_button2, 70, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_story_choice_button2, ui_dual_gif_story_choice_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)2);

    s_story_choice_button2_label = lv_label_create(s_story_choice_button2);
    lv_obj_set_width(s_story_choice_button2_label, 329);
    lv_obj_set_height(s_story_choice_button2_label, LV_SIZE_CONTENT);
    lv_obj_set_x(s_story_choice_button2_label, 60);
    lv_obj_set_y(s_story_choice_button2_label, 211);
    lv_obj_set_align(s_story_choice_button2_label, LV_ALIGN_CENTER);
    lv_label_set_text(s_story_choice_button2_label, "");
    lv_obj_set_style_text_color(s_story_choice_button2_label, lv_color_hex(0x490909), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(s_story_choice_button2_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_story_choice_button2_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_story_choice_button2_label, &ui_font_Font33, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_story_choice_button2_disabled_tint = lv_obj_create(s_story_choice_button2);
    lv_obj_remove_style_all(s_story_choice_button2_disabled_tint);
    lv_obj_set_size(s_story_choice_button2_disabled_tint, lv_pct(100), lv_pct(100));
    lv_obj_center(s_story_choice_button2_disabled_tint);
    lv_obj_clear_flag(s_story_choice_button2_disabled_tint,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_story_choice_button2_disabled_tint,
                    LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(s_story_choice_button2_disabled_tint, 100, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_story_choice_button2_disabled_tint,
                              lv_color_hex(0x7A3E1D),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_story_choice_button2_disabled_tint, 55, LV_PART_MAIN);

    ui_dual_gif_create_post_close_panels();

    s_post_close_label = lv_label_create(s_dual_gif_screen);
    lv_obj_set_width(s_post_close_label, LV_SIZE_CONTENT);
    lv_obj_set_height(s_post_close_label, LV_SIZE_CONTENT);
    lv_obj_set_x(s_post_close_label, -295);
    lv_obj_set_y(s_post_close_label, -167);
    lv_obj_set_align(s_post_close_label, LV_ALIGN_CENTER);
    lv_label_set_text(s_post_close_label, s_post_close_label_text);
    lv_obj_set_style_text_color(s_post_close_label, lv_color_hex(0xC1B226), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(s_post_close_label, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_post_close_label, &ui_font_Font4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_transform_angle(s_post_close_label, -150, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_transform_zoom(s_post_close_label, 256, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_opa(s_post_close_label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(s_post_close_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_post_close_label,
                        ui_dual_gif_cloud_button_event_cb,
                        LV_EVENT_ALL,
                        (void *)(intptr_t)0);
    lv_obj_add_flag(s_post_close_label, LV_OBJ_FLAG_HIDDEN);

    s_corner1_img = lv_img_create(s_book_overlay_layer);
    lv_img_set_src(s_corner1_img, &ui_img_corner_small_crop_png);
    lv_obj_align(s_corner1_img, LV_ALIGN_CENTER, UI_DUAL_GIF_CORNER1_OFFSET_X, UI_DUAL_GIF_CORNER1_OFFSET_Y);

    s_corner2_img = lv_img_create(s_book_overlay_layer);
    lv_img_set_src(s_corner2_img, &ui_img_corner_small_crop_png);
    lv_obj_align(s_corner2_img, LV_ALIGN_CENTER, UI_DUAL_GIF_CORNER2_OFFSET_X, UI_DUAL_GIF_CORNER2_OFFSET_Y);
    lv_img_set_angle(s_corner2_img, UI_DUAL_GIF_CORNER2_ANGLE);

    s_corner3_img = lv_img_create(s_book_overlay_layer);
    lv_img_set_src(s_corner3_img, &ui_img_corner_small_crop_png);
    lv_obj_align(s_corner3_img, LV_ALIGN_CENTER, UI_DUAL_GIF_CORNER3_OFFSET_X, UI_DUAL_GIF_CORNER3_OFFSET_Y);
    lv_img_set_angle(s_corner3_img, UI_DUAL_GIF_CORNER3_ANGLE);

    s_corner4_img = lv_img_create(s_book_overlay_layer);
    lv_img_set_src(s_corner4_img, &ui_img_corner_small_crop_png);
    lv_obj_align(s_corner4_img, LV_ALIGN_CENTER, UI_DUAL_GIF_CORNER4_OFFSET_X, UI_DUAL_GIF_CORNER4_OFFSET_Y);
    lv_img_set_angle(s_corner4_img, UI_DUAL_GIF_CORNER4_ANGLE);

    s_settings_screen = lv_obj_create(s_dual_gif_screen);
    lv_obj_remove_style_all(s_settings_screen);
    lv_obj_set_size(s_settings_screen, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_align(s_settings_screen, LV_ALIGN_CENTER);
    lv_obj_clear_flag(s_settings_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_settings_screen, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_settings_screen, lv_color_hex(0x171717), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_settings_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_settings_close_button = lv_imgbtn_create(s_settings_screen);
    lv_imgbtn_set_src(s_settings_close_button, LV_IMGBTN_STATE_RELEASED, NULL, &ui_img_sett_off_png, NULL);
    lv_imgbtn_set_src(s_settings_close_button, LV_IMGBTN_STATE_PRESSED, NULL, &ui_img_sett_off_png, NULL);
    lv_obj_set_size(s_settings_close_button, 64, 64);
    lv_obj_set_x(s_settings_close_button, -477);
    lv_obj_set_y(s_settings_close_button, -265);
    lv_obj_set_align(s_settings_close_button, LV_ALIGN_CENTER);
    lv_obj_clear_flag(s_settings_close_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_settings_close_button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_settings_close_button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_settings_close_button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_settings_close_button, ui_dual_gif_settings_close_event_cb, LV_EVENT_CLICKED, NULL);

    {
        lv_obj_t *container6 = lv_obj_create(s_settings_screen);
        lv_obj_t *container11 = NULL;
        lv_obj_t *container7 = NULL;
        lv_obj_t *label = NULL;

        lv_obj_remove_style_all(container6);
        lv_obj_set_height(container6, 525);
        lv_obj_set_width(container6, lv_pct(100));
        lv_obj_set_x(container6, -1);
        lv_obj_set_y(container6, 33);
        lv_obj_set_align(container6, LV_ALIGN_CENTER);
        lv_obj_set_flex_flow(container6, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(container6,
                              LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(container6, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        container11 = lv_obj_create(container6);
        lv_obj_remove_style_all(container11);
        lv_obj_set_width(container11, 435);
        lv_obj_set_height(container11, lv_pct(45));
        lv_obj_set_align(container11, LV_ALIGN_CENTER);
        lv_obj_clear_flag(container11, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        label = lv_label_create(container11);
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        lv_obj_set_y(label, -80);
        lv_label_set_text(label, "Volume");
        lv_obj_set_style_text_color(label, lv_color_hex(0xECCA5A), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &ui_font_Font6, LV_PART_MAIN | LV_STATE_DEFAULT);

        s_settings_volume_icon = lv_img_create(container11);
        lv_img_set_src(s_settings_volume_icon, &ui_img_vol_png);
        lv_obj_set_align(s_settings_volume_icon, LV_ALIGN_CENTER);
        lv_obj_set_y(s_settings_volume_icon, -17);
        lv_obj_add_flag(s_settings_volume_icon, LV_OBJ_FLAG_ADV_HITTEST);
        lv_obj_clear_flag(s_settings_volume_icon, LV_OBJ_FLAG_SCROLLABLE);

        s_settings_volume_slider = lv_slider_create(container11);
        lv_slider_set_range(s_settings_volume_slider,
                            UI_DUAL_GIF_SETTINGS_VOLUME_MIN,
                            UI_DUAL_GIF_SETTINGS_VOLUME_MAX);
        lv_slider_set_value(s_settings_volume_slider, audio_session_get_volume(), LV_ANIM_OFF);
        lv_obj_set_width(s_settings_volume_slider, 328);
        lv_obj_set_height(s_settings_volume_slider, 18);
        lv_obj_set_x(s_settings_volume_slider, 5);
        lv_obj_set_y(s_settings_volume_slider, 53);
        lv_obj_set_align(s_settings_volume_slider, LV_ALIGN_CENTER);
        ui_dual_gif_style_settings_slider(s_settings_volume_slider);
        lv_obj_add_event_cb(s_settings_volume_slider, ui_dual_gif_settings_volume_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(s_settings_volume_slider, ui_dual_gif_settings_volume_slider_event_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(s_settings_volume_slider, ui_dual_gif_settings_volume_slider_event_cb, LV_EVENT_PRESS_LOST, NULL);

        container7 = lv_obj_create(container6);
        lv_obj_remove_style_all(container7);
        lv_obj_set_width(container7, 435);
        lv_obj_set_height(container7, lv_pct(45));
        lv_obj_set_align(container7, LV_ALIGN_CENTER);
        lv_obj_clear_flag(container7, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        label = lv_label_create(container7);
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        lv_obj_set_y(label, -80);
        lv_label_set_text(label, "Brightness");
        lv_obj_set_style_text_color(label, lv_color_hex(0xECCA5A), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &ui_font_Font6, LV_PART_MAIN | LV_STATE_DEFAULT);

        s_settings_brightness_icon = lv_img_create(container7);
        lv_img_set_src(s_settings_brightness_icon, &ui_img_bright_png);
        lv_obj_set_align(s_settings_brightness_icon, LV_ALIGN_CENTER);
        lv_obj_set_y(s_settings_brightness_icon, -17);
        lv_obj_add_flag(s_settings_brightness_icon, LV_OBJ_FLAG_ADV_HITTEST);
        lv_obj_clear_flag(s_settings_brightness_icon, LV_OBJ_FLAG_SCROLLABLE);

        s_settings_brightness_slider = lv_slider_create(container7);
        lv_slider_set_range(s_settings_brightness_slider,
                            UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MIN,
                            UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MAX);
        lv_slider_set_value(s_settings_brightness_slider,
                            ui_dual_gif_clamp_int(bsp_display_brightness_get(),
                                                  UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MIN,
                                                  UI_DUAL_GIF_SETTINGS_BRIGHTNESS_MAX),
                            LV_ANIM_OFF);
        lv_obj_set_width(s_settings_brightness_slider, 328);
        lv_obj_set_height(s_settings_brightness_slider, 18);
        lv_obj_set_x(s_settings_brightness_slider, 5);
        lv_obj_set_y(s_settings_brightness_slider, 53);
        lv_obj_set_align(s_settings_brightness_slider, LV_ALIGN_CENTER);
        ui_dual_gif_style_settings_slider(s_settings_brightness_slider);
        lv_obj_add_event_cb(s_settings_brightness_slider, ui_dual_gif_settings_brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(s_settings_brightness_slider, ui_dual_gif_settings_brightness_slider_event_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(s_settings_brightness_slider, ui_dual_gif_settings_brightness_slider_event_cb, LV_EVENT_PRESS_LOST, NULL);

#if 0 /* Full-app restart and user-name controls are intentionally absent. */
        lv_obj_t *container9 = NULL;
        lv_obj_t *container8 = NULL;
        container9 = lv_obj_create(container6);
        lv_obj_remove_style_all(container9);
        lv_obj_set_width(container9, 435);
        lv_obj_set_height(container9, lv_pct(55));
        lv_obj_set_y(container9, -6);
        lv_obj_set_align(container9, LV_ALIGN_CENTER);
        lv_obj_clear_flag(container9, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        label = lv_label_create(container9);
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        lv_obj_set_y(label, -106);
        lv_label_set_text(label, "Restart app");
        lv_obj_set_style_text_color(label, lv_color_hex(0xECCA5A), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &ui_font_Font6, LV_PART_MAIN | LV_STATE_DEFAULT);

        {
            lv_obj_t *icon = lv_img_create(container9);
            lv_img_set_src(icon, &ui_img_restart_png);
            lv_obj_set_align(icon, LV_ALIGN_CENTER);
            lv_obj_set_y(icon, -42);
            lv_obj_add_flag(icon, LV_OBJ_FLAG_ADV_HITTEST);
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        }

        label = lv_label_create(container9);
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        lv_obj_set_y(label, 18);
        lv_label_set_text(label, "Start over from the splash screen");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &ui_font_Font52, LV_PART_MAIN | LV_STATE_DEFAULT);

        s_settings_restart_button = lv_btn_create(container9);
        lv_obj_set_size(s_settings_restart_button, 145, 70);
        lv_obj_set_x(s_settings_restart_button, 0);
        lv_obj_set_y(s_settings_restart_button, 93);
        lv_obj_set_align(s_settings_restart_button, LV_ALIGN_CENTER);
        lv_obj_add_flag(s_settings_restart_button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_clear_flag(s_settings_restart_button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(s_settings_restart_button, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(s_settings_restart_button, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(s_settings_restart_button, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(s_settings_restart_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        ui_dual_gif_style_yellow_button_pressed_shadow(s_settings_restart_button);
        lv_obj_add_event_cb(s_settings_restart_button, ui_dual_gif_settings_restart_event_cb, LV_EVENT_CLICKED, NULL);

        label = lv_label_create(s_settings_restart_button);
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        lv_label_set_text(label, "Restart");
        lv_obj_set_style_text_color(label, lv_color_hex(0x171717), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &ui_font_Font51, LV_PART_MAIN | LV_STATE_DEFAULT);

        container8 = lv_obj_create(container6);
        lv_obj_remove_style_all(container8);
        lv_obj_set_width(container8, 435);
        lv_obj_set_height(container8, lv_pct(55));
        lv_obj_set_y(container8, -6);
        lv_obj_set_align(container8, LV_ALIGN_CENTER);
        lv_obj_clear_flag(container8, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        label = lv_label_create(container8);
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        lv_obj_set_y(label, -106);
        lv_label_set_text(label, "User Name");
        lv_obj_set_style_text_color(label, lv_color_hex(0xECCA5A), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &ui_font_Font6, LV_PART_MAIN | LV_STATE_DEFAULT);

        {
            lv_obj_t *icon = lv_img_create(container8);
            lv_img_set_src(icon, &ui_img_name_png);
            lv_obj_set_align(icon, LV_ALIGN_CENTER);
            lv_obj_set_y(icon, -42);
            lv_obj_add_flag(icon, LV_OBJ_FLAG_ADV_HITTEST);
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        }

        label = lv_label_create(container8);
        lv_obj_set_x(label, -46);
        lv_obj_set_y(label, 18);
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        lv_label_set_text(label, "Current name:");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &ui_font_Font52, LV_PART_MAIN | LV_STATE_DEFAULT);

        s_settings_user_name_value_label = lv_label_create(container8);
        lv_obj_set_width(s_settings_user_name_value_label, 168);
        lv_obj_set_x(s_settings_user_name_value_label, 121);
        lv_obj_set_y(s_settings_user_name_value_label, 19);
        lv_obj_set_align(s_settings_user_name_value_label, LV_ALIGN_CENTER);
        lv_label_set_long_mode(s_settings_user_name_value_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(s_settings_user_name_value_label, UI_DUAL_GIF_SETTINGS_NAME_EMPTY);
        lv_obj_set_style_text_color(s_settings_user_name_value_label, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(s_settings_user_name_value_label, &ui_font_Font52, LV_PART_MAIN | LV_STATE_DEFAULT);

        s_settings_clear_name_button = lv_btn_create(container8);
        lv_obj_set_size(s_settings_clear_name_button, 145, 70);
        lv_obj_set_x(s_settings_clear_name_button, 0);
        lv_obj_set_y(s_settings_clear_name_button, 93);
        lv_obj_set_align(s_settings_clear_name_button, LV_ALIGN_CENTER);
        lv_obj_add_flag(s_settings_clear_name_button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_clear_flag(s_settings_clear_name_button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(s_settings_clear_name_button, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(s_settings_clear_name_button, lv_color_hex(0xFAF190), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(s_settings_clear_name_button, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(s_settings_clear_name_button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        ui_dual_gif_style_yellow_button_pressed_shadow(s_settings_clear_name_button);
        lv_obj_add_event_cb(s_settings_clear_name_button, ui_dual_gif_settings_clear_name_event_cb, LV_EVENT_CLICKED, NULL);

        label = lv_label_create(s_settings_clear_name_button);
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        lv_label_set_text(label, "Clear");
        lv_obj_set_style_text_color(label, lv_color_hex(0x171717), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &ui_font_Font51, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_move_to_index(container8, 2);
#endif
    }

    ui_dual_gif_refresh_settings_controls();
}

void ui_dual_gif_screen_register_callbacks(const ui_dual_gif_screen_callbacks_t *callbacks)
{
    if (!callbacks) {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
        return;
    }

    s_callbacks = *callbacks;
}

static void ui_dual_gif_reset_story_state_internal(bool keep_active_open_cfa)
{
    bool placeholder_ready = false;
    const bool keep_open_cfa = keep_active_open_cfa &&
                               s_book_player.gif &&
                               !s_book_player.task &&
                               ui_dual_gif_decoder_is_cfa(s_book_player.gif) &&
                               animation_decoder_frame_ready(s_book_player.gif) &&
                               ui_dual_gif_paths_match(s_book_player.path, UI_DUAL_GIF_OPEN_PATH);

    ui_dual_gif_prepare_screen();
    if (!keep_open_cfa) {
        ui_dual_gif_prepare_player_reopen_for_path(&s_book_player, UI_DUAL_GIF_OPEN_PATH);
    }
    ui_dual_gif_configure_book_action(UI_DUAL_GIF_BOOK_ACTION_NONE);
    if (keep_open_cfa) {
        ESP_LOGI(TAG,
                 "keep active book cfa path=%s reason=open_start",
                 s_book_player.path ? s_book_player.path : "(null)");
        placeholder_ready = true;
    } else {
        placeholder_ready = ui_dual_gif_open_player(&s_book_player);
    }
    s_book_animation_started_us = 0;
    s_scene_ready_pending = false;
    s_scene_overlay_ready = false;
    s_book_transition_ready = false;
    s_current_scene_is_final = false;
    s_first_open_text_delay_pending = true;
    s_bird_player.entry_anim_started = false;
    s_story_choice_waiting = false;
    s_show_choice_after_text_fade = false;

    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "LVGL lock timed out while resetting story state");
        return;
    }

    ui_dual_gif_cancel_story_text_timers();
    ui_dual_gif_cancel_story_text_anim();
    ui_dual_gif_hide_story_text();
    ui_dual_gif_clear_story_text_content();
    ui_dual_gif_cancel_story_choice_timers();
    ui_dual_gif_cancel_story_choice_anim();
    ui_dual_gif_hide_story_img_button();
    ui_dual_gif_hide_story_choice();
    ui_dual_gif_hide_cloud();
    ui_dual_gif_reset_overlay_state();
    (void)ui_dual_gif_set_bird_state_now(UI_DUAL_GIF_BIRD_INITIAL_STATE);
    if (placeholder_ready) {
        /* The image descriptor and its backing buffer are reused across story
         * resets.  Invalidate LVGL's image cache before exposing the freshly
         * rendered placeholder, otherwise the first draw can mix cached lines
         * with the new frame until the opening animation invalidates them. */
        (void)ui_dual_gif_attach_current_frame_on_ui_thread(&s_book_player);
    }

    lvgl_port_unlock();
}

static void ui_dual_gif_reset_story_state_now(void)
{
    ui_dual_gif_cancel_deferred_book_preload();
    ui_dual_gif_reset_story_state_internal(false);
}

static void ui_dual_gif_start_open_now(void)
{
    ui_dual_gif_cancel_deferred_book_preload();
    ui_dual_gif_reset_story_state_internal(true);
    ui_dual_gif_configure_book_action(UI_DUAL_GIF_BOOK_ACTION_OPEN);
    s_scene_ready_pending = true;
    s_scene_overlay_ready = false;
    s_book_transition_ready = false;
    ui_dual_gif_start_player(&s_book_player);
}

static void ui_dual_gif_start_swipe_now(void)
{
    ui_dual_gif_prepare_player_reopen_for_path(&s_book_player, UI_DUAL_GIF_SWIPE_PATH);

    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "LVGL lock timed out while preparing swipe");
        return;
    }

    ui_dual_gif_cancel_story_choice_timers();
    ui_dual_gif_cancel_story_choice_anim();
    ui_dual_gif_hide_story_img_button();
    ui_dual_gif_hide_story_choice();
    lvgl_port_unlock();

    ui_dual_gif_configure_book_action(UI_DUAL_GIF_BOOK_ACTION_SWIPE);
    s_scene_ready_pending = true;
    s_scene_overlay_ready = false;
    s_book_transition_ready = false;
    ui_dual_gif_start_player(&s_book_player);
    (void)ui_dual_gif_preload_path(&s_book_player, s_current_scene_is_final ? UI_DUAL_GIF_CLOSE_PATH : UI_DUAL_GIF_SWIPE_PATH);
}

static void ui_dual_gif_start_close_now(void)
{
    ui_dual_gif_prepare_player_reopen_for_path(&s_book_player, UI_DUAL_GIF_CLOSE_PATH);

    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "LVGL lock timed out while preparing close");
        return;
    }

    ui_dual_gif_cancel_cloud_anim();
    ui_dual_gif_hide_story_img_button();
    lvgl_port_unlock();

    ui_dual_gif_configure_book_action(UI_DUAL_GIF_BOOK_ACTION_CLOSE);
    s_scene_ready_pending = false;
    s_scene_overlay_ready = false;
    s_book_transition_ready = false;
    ui_dual_gif_start_player(&s_book_player);
    (void)ui_dual_gif_defer_book_preload_until_audio_stable(UI_DUAL_GIF_OPEN_PATH);
}

static void ui_dual_gif_apply_story_content_now(const ui_dual_gif_story_content_t *content)
{
    ui_dual_gif_apply_story_text_content_now(content, false);

    if (s_a_small_img) {
        const void *current_src = lv_img_get_src(s_a_small_img);

        ESP_LOGI(TAG,
                 "scene_apply_to_img case=%d node=%s uri=%s size=%u",
                 content ? content->debug_case_id : -1,
                 content && content->debug_node_name ? content->debug_node_name : "<null>",
                 content && content->debug_image_uri ? content->debug_image_uri : "<null>",
                 s_overlay_story_img ? (unsigned)s_overlay_story_img->data_size : 0U);
        if (current_src != s_overlay_story_img) {
            ui_dual_gif_invalidate_overlay_src(current_src, "apply_to_img_prev");
        }
        ui_dual_gif_invalidate_overlay_src(s_overlay_story_img, "apply_to_img_next");
        lv_img_set_src(s_a_small_img, s_overlay_story_img);
        ui_dual_gif_log_overlay_runtime_state("apply_to_img", content);
        ui_dual_gif_force_overlay_redraw("apply_to_img");
    }
}

static void ui_dual_gif_apply_story_text_content_now(const ui_dual_gif_story_content_t *content, bool force_reset)
{
    const char *reading_text = "";
    const char *question_text = "";
    const char *choice1_text = "";
    const char *choice2_text = "";
    bool content_changed = force_reset;

    if (content) {
        reading_text = content->reading_text ? content->reading_text : "";
        question_text = content->question_text ? content->question_text : "";
        choice1_text = content->choice1_text ? content->choice1_text : "";
        choice2_text = content->choice2_text ? content->choice2_text : "";
    }

    if (s_story_choice_button2) {
        if (content && !content->choice2_enabled) {
            lv_obj_add_state(s_story_choice_button2, LV_STATE_DISABLED);
            if (s_story_choice_button2_disabled_tint) {
                lv_obj_clear_flag(s_story_choice_button2_disabled_tint, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(s_story_choice_button2_disabled_tint);
            }
        } else {
            lv_obj_clear_state(s_story_choice_button2, LV_STATE_DISABLED);
            if (s_story_choice_button2_disabled_tint) {
                lv_obj_add_flag(s_story_choice_button2_disabled_tint, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (!content_changed) {
        const char *current_reading = s_story_text_label ? lv_label_get_text(s_story_text_label) : "";
        const char *current_question = s_story_choice_question_label ? lv_label_get_text(s_story_choice_question_label) : "";
        const char *current_choice1 = s_story_choice_button1_label ? lv_label_get_text(s_story_choice_button1_label) : "";
        const char *current_choice2 = s_story_choice_button2_label ? lv_label_get_text(s_story_choice_button2_label) : "";

        if (strcmp(current_reading ? current_reading : "", reading_text) != 0 ||
            strcmp(current_question ? current_question : "", question_text) != 0 ||
            strcmp(current_choice1 ? current_choice1 : "", choice1_text) != 0 ||
            strcmp(current_choice2 ? current_choice2 : "", choice2_text) != 0) {
            content_changed = true;
        }
    }

    if (!content_changed) {
        return;
    }

    if (s_story_text_label) {
        s_story_text_auto_scroll_offset = 0;
        lv_label_set_text(s_story_text_label, reading_text);
        if (s_story_text_view) {
            lv_obj_scroll_to(s_story_text_view, 0, 0, LV_ANIM_OFF);
        }
        ui_dual_gif_layout_story_text();
        ui_dual_gif_maybe_start_story_text_auto_scroll();
    }
    if (s_story_choice_question_label) {
        lv_label_set_text(s_story_choice_question_label, question_text);
    }
    if (s_story_choice_button1_label) {
        lv_label_set_text(s_story_choice_button1_label, choice1_text);
    }
    if (s_story_choice_button2_label) {
        lv_label_set_text(s_story_choice_button2_label, choice2_text);
    }
}

void ui_dual_gif_screen_set_story_content(const ui_dual_gif_story_content_t *content)
{
    ui_dual_gif_prepare_screen();

    memset(&s_staged_story_content, 0, sizeof(s_staged_story_content));
    if (content) {
        s_staged_story_content = *content;
        s_default_overlay_img = content->overlay_image;
        s_overlay_story_img = content->overlay_image;
        s_current_scene_is_final = content->is_final_scene;
        ESP_LOGI(TAG,
                 "scene_stage case=%d node=%s uri=%s size=%u final=%u",
                 content->debug_case_id,
                 content->debug_node_name ? content->debug_node_name : "<null>",
                 content->debug_image_uri ? content->debug_image_uri : "<null>",
                 content->overlay_image ? (unsigned)content->overlay_image->data_size : 0U,
                 content->is_final_scene ? 1U : 0U);
    } else {
        s_default_overlay_img = NULL;
        s_overlay_story_img = NULL;
        s_current_scene_is_final = false;
        UI_DIAG_LOGI("scene_stage case=-1 node=<null> uri=<null> size=0 final=0");
    }
}

void ui_dual_gif_screen_commit_story_content(void)
{
    ui_dual_gif_prepare_screen();
    UI_DIAG_LOGI("scene_commit case=%d node=%s uri=%s size=%u",
                 s_staged_story_content.debug_case_id,
                 s_staged_story_content.debug_node_name ? s_staged_story_content.debug_node_name : "<null>",
                 s_staged_story_content.debug_image_uri ? s_staged_story_content.debug_image_uri : "<null>",
                 s_staged_story_content.overlay_image ? (unsigned)s_staged_story_content.overlay_image->data_size : 0U);
    ui_dual_gif_log_overlay_runtime_state("commit_before_apply", &s_staged_story_content);
    ui_dual_gif_apply_story_content_now(&s_staged_story_content);
}

void ui_dual_gif_screen_reset_story_state(void)
{
    ui_dual_gif_prepare_screen();
    if (!ui_dual_gif_post_control_command(UI_DUAL_GIF_CTRL_CMD_RESET_STORY, 0U)) {
        ui_dual_gif_reset_story_state_now();
    }
}

void ui_dual_gif_screen_start_open(void)
{
    ui_dual_gif_prepare_screen();
    if (!ui_dual_gif_post_control_command(UI_DUAL_GIF_CTRL_CMD_START_OPEN, 0U)) {
        ui_dual_gif_start_open_now();
    }
}

void ui_dual_gif_screen_start_swipe(void)
{
    ui_dual_gif_prepare_screen();
    if (!ui_dual_gif_post_control_command(UI_DUAL_GIF_CTRL_CMD_START_SWIPE, 0U)) {
        ui_dual_gif_start_swipe_now();
    }
}

void ui_dual_gif_screen_start_close(void)
{
    ui_dual_gif_prepare_screen();
    if (!ui_dual_gif_post_control_command(UI_DUAL_GIF_CTRL_CMD_START_CLOSE, 0U)) {
        ui_dual_gif_start_close_now();
    }
}

void ui_dual_gif_screen_show_choices(void)
{
    s_story_choice_waiting = true;
    s_show_choice_after_text_fade = true;

    if (s_story_text_view && !lv_obj_has_flag(s_story_text_view, LV_OBJ_FLAG_HIDDEN)) {
        ui_dual_gif_schedule_story_text_fade_out(UI_DUAL_GIF_STORY_TEXT_FADE_OUT_DELAY_MS);
        return;
    }

    s_show_choice_after_text_fade = false;
    ui_dual_gif_show_story_choice();
}

void ui_dual_gif_screen_set_choice_pressed(int choice_index, bool pressed)
{
    lv_obj_t *button = choice_index == 1 ? s_story_choice_button1 : s_story_choice_button2;

    if (!button || (pressed && !s_story_choice_waiting)) {
        return;
    }

    if (pressed) {
        lv_obj_add_state(button, LV_STATE_PRESSED);
    } else {
        lv_obj_clear_state(button, LV_STATE_PRESSED);
    }
    lv_obj_invalidate(button);
}

void ui_dual_gif_screen_click_choice(int choice_index)
{
    lv_obj_t *button = choice_index == 1 ? s_story_choice_button1 : s_story_choice_button2;

    if (!button || !s_story_choice_waiting) {
        return;
    }

    lv_obj_clear_state(button, LV_STATE_PRESSED);
    lv_obj_invalidate(button);
    (void)lv_event_send(button, LV_EVENT_CLICKED, NULL);
}

void ui_dual_gif_screen_preload_close(void)
{
    ui_dual_gif_prepare_screen();
    (void)ui_dual_gif_preload_path(&s_book_player, UI_DUAL_GIF_CLOSE_PATH);
}

void ui_dual_gif_screen_show_cloud(void)
{
    ui_dual_gif_show_cloud();
}

void ui_dual_gif_screen_set_restart_pressed(bool pressed)
{
    LV_UNUSED(pressed);
}

void ui_dual_gif_screen_click_restart(void)
{
    if (!s_post_close_label || lv_obj_has_flag(s_post_close_label, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    ui_dual_gif_screen_set_restart_pressed(false);
    (void)lv_event_send(s_post_close_label, LV_EVENT_RELEASED, NULL);
}

void ui_dual_gif_screen_set_audio_active(bool active)
{
    lv_coord_t current_scroll = 0;

    ui_dual_gif_prepare_screen();

    if (s_audio_active == active) {
        if (active) {
            ui_dual_gif_maybe_start_story_text_auto_scroll();
        }
        return;
    }

    s_audio_active = active;

    if (active) {
        if (s_story_text_view) {
            current_scroll = lv_obj_get_scroll_y(s_story_text_view);
            if (current_scroll > 0) {
                s_story_text_auto_scroll_offset = current_scroll;
                lv_obj_scroll_to_y(s_story_text_view, 0, LV_ANIM_OFF);
                ui_dual_gif_layout_story_text();
            }
        }
        ui_dual_gif_maybe_start_story_text_auto_scroll();
        return;
    }

    ui_dual_gif_cancel_story_text_auto_scroll();

    if (s_deferred_book_preload_path) {
        ui_dual_gif_arm_deferred_book_preload_timer();
    }
}

void ui_dual_gif_screen_set_bird_state(ui_dual_gif_bird_state_t state)
{
    ui_dual_gif_prepare_screen();
    (void)ui_dual_gif_set_bird_state_now(state);
}

void ui_dual_gif_screen_request_bird_state(ui_dual_gif_bird_state_t state)
{
    if (!s_dual_gif_screen || !s_bird_player.img_obj) {
        return;
    }

    (void)ui_dual_gif_request_bird_state(state);
}

void ui_dual_gif_screen_set_bird_talking(bool talking)
{
    ui_dual_gif_screen_set_bird_state(talking ? UI_DUAL_GIF_BIRD_STATE_TALK_LOOP :
                                                UI_DUAL_GIF_BIRD_STATE_IDLE);
}

void ui_dual_gif_screen_init(void)
{
    ui_dual_gif_prepare_screen();
    ui_dual_gif_reset_story_state_now();
}

void ui_dual_gif_screen_preload_initial_media(void)
{
    ui_dual_gif_screen_preload_initial_media_with_progress(NULL, NULL);
}

static void ui_dual_gif_report_preload_progress(ui_dual_gif_preload_progress_cb_t progress_cb,
                                                void *user_ctx,
                                                uint8_t percent,
                                                const char *stage)
{
    if (progress_cb) {
        progress_cb(percent, stage, user_ctx);
    }
}

void ui_dual_gif_screen_preload_initial_media_with_progress(
    ui_dual_gif_preload_progress_cb_t progress_cb,
    void *user_ctx)
{
    if (s_initial_media_preload_complete) {
        ui_dual_gif_report_preload_progress(progress_cb, user_ctx, 100U, "Story ready");
        return;
    }

    ui_dual_gif_ensure_animation_cache();
    ui_dual_gif_report_preload_progress(progress_cb, user_ctx, 50U, "Preparing book opening");
    (void)ui_dual_gif_preload_path(&s_book_player, UI_DUAL_GIF_OPEN_PATH);
    (void)ui_dual_gif_require_book_cfa_preload(&s_book_player, UI_DUAL_GIF_OPEN_PATH);
    ui_dual_gif_report_preload_progress(progress_cb, user_ctx, 90U, "Preparing bird");
    (void)ui_dual_gif_preload_path(&s_bird_player, UI_DUAL_GIF_BIRD_IDLE_TEST_PATH);
    (void)ui_dual_gif_wait_for_matching_preload(&s_bird_player, UI_DUAL_GIF_BIRD_IDLE_TEST_PATH);
    s_initial_media_preload_complete = true;
    ui_dual_gif_report_preload_progress(progress_cb, user_ctx, 100U, "Story ready");
}

lv_obj_t *ui_dual_gif_screen_root(void)
{
    ui_dual_gif_prepare_screen();
    return s_dual_gif_screen;
}

static void ui_dual_gif_release_screen_state(void)
{
    s_dual_gif_screen = NULL;
    s_book_player.img_obj = NULL;
    s_bird_player.img_obj = NULL;
    s_settings_confirm_overlay = NULL;
    s_settings_toast = NULL;
    s_settings_toast_label = NULL;

    ui_dual_gif_cancel_story_text_timers();
    ui_dual_gif_cancel_story_text_anim();
    ui_dual_gif_cancel_story_text_auto_scroll();
    ui_dual_gif_cancel_story_choice_timers();
    ui_dual_gif_cancel_story_choice_anim();
    ui_dual_gif_cancel_cloud_anim();
    ui_dual_gif_cancel_deferred_book_preload();
    if (s_settings_toast_timer) {
        lv_timer_del(s_settings_toast_timer);
        s_settings_toast_timer = NULL;
    }
    s_audio_active = false;
    s_story_text_auto_scroll_offset = 0;
    if (s_bird_frame_timer) {
        lv_timer_del(s_bird_frame_timer);
        s_bird_frame_timer = NULL;
    }
    ui_dual_gif_free_player(&s_book_player);
    ui_dual_gif_free_player(&s_bird_player);
    ui_dual_gif_reset_overlay_state();
    s_book_animation_started_us = 0;
    ui_dual_gif_reset_bird_player_state(&s_bird_player, UI_DUAL_GIF_BIRD_INITIAL_STATE);

    s_decor_bg_canvas = NULL;
    s_book_img = NULL;
    s_bird_img = NULL;
    s_book_overlay_layer = NULL;
    s_a_small_img = NULL;
    s_story_text_view = NULL;
    s_story_text_label = NULL;
    s_story_choice_container = NULL;
    s_story_choice_question_panel = NULL;
    s_story_choice_question_label = NULL;
    s_story_choice_options_container = NULL;
    s_story_choice_button1 = NULL;
    s_story_choice_button1_label = NULL;
    s_story_choice_button2 = NULL;
    s_story_choice_button2_label = NULL;
    s_story_choice_button2_disabled_tint = NULL;
    s_story_img_button = NULL;
    s_settings_screen = NULL;
    s_settings_close_button = NULL;
    s_settings_volume_icon = NULL;
    s_settings_volume_slider = NULL;
    s_settings_brightness_icon = NULL;
    s_settings_brightness_slider = NULL;
    s_settings_user_name_value_label = NULL;
    s_settings_restart_button = NULL;
    s_settings_clear_name_button = NULL;
    s_settings_confirm_overlay = NULL;
    s_settings_toast = NULL;
    s_settings_toast_label = NULL;
    memset(s_post_close_panels, 0, sizeof(s_post_close_panels));
    s_post_close_label = NULL;
    s_corner1_img = NULL;
    s_corner2_img = NULL;
    s_corner3_img = NULL;
    s_corner4_img = NULL;
    s_story_text_fade_in_timer = NULL;
    s_story_text_fade_out_timer = NULL;
    s_story_text_auto_scroll_timer = NULL;
    s_story_choice_fade_in_timer = NULL;
    s_story_choice_fade_out_timer = NULL;
    s_bird_frame_timer = NULL;
    s_book_preload_defer_timer = NULL;
    s_settings_toast_timer = NULL;
    s_deferred_book_preload_path = NULL;
    s_story_choice_waiting = false;
    s_show_choice_after_text_fade = false;
    s_audio_active = false;
    s_scene_ready_pending = false;
    s_current_scene_is_final = false;
    s_first_open_text_delay_pending = true;
    s_book_action = UI_DUAL_GIF_BOOK_ACTION_NONE;
    s_settings_confirm_action = UI_DUAL_GIF_SETTINGS_CONFIRM_NONE;
}

static void ui_dual_gif_screen_delete_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }

    ui_dual_gif_release_screen_state();
}

void ui_dual_gif_screen_destroy(void)
{
    if (s_dual_gif_screen) {
        lv_obj_del(s_dual_gif_screen);
    } else {
        ui_dual_gif_release_screen_state();
    }

    if (s_decor_bg_buf) {
        heap_caps_free(s_decor_bg_buf);
        s_decor_bg_buf = NULL;
    }

    ui_dual_gif_release_decor_assets();

    if (s_control_task) {
        vTaskDelete(s_control_task);
        s_control_task = NULL;
    }
    if (s_control_queue) {
        vQueueDelete(s_control_queue);
        s_control_queue = NULL;
    }
    if (s_book_player.task_done_sem) {
        vSemaphoreDelete(s_book_player.task_done_sem);
        s_book_player.task_done_sem = NULL;
    }
    if (s_bird_player.task_done_sem) {
        vSemaphoreDelete(s_bird_player.task_done_sem);
        s_bird_player.task_done_sem = NULL;
    }

    media_cache_deinit(&s_animation_cache);
    s_initial_media_preload_complete = false;
    ui_dual_gif_release_a_small_bin();
}
