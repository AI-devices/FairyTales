#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *reading_text;
    const char *question_text;
    const char *choice1_text;
    const char *choice2_text;
    const lv_img_dsc_t *overlay_image;
    int debug_case_id;
    const char *debug_node_name;
    const char *debug_image_uri;
    bool is_final_scene;
    bool choice2_enabled;
} ui_dual_gif_story_content_t;

typedef enum
{
    UI_DUAL_GIF_BIRD_STATE_IDLE = 0,
    UI_DUAL_GIF_BIRD_STATE_PRE_TALK,
    UI_DUAL_GIF_BIRD_STATE_TALK_LOOP,
} ui_dual_gif_bird_state_t;

typedef struct
{
    void (*scene_image_ready)(void *user_ctx);
    void (*choice_selected)(int choice_index, void *user_ctx);
    void (*close_finished)(void *user_ctx);
    void (*restart_requested)(void *user_ctx);
    void (*settings_opened)(void *user_ctx);
    void (*settings_closed)(void *user_ctx);
    void *user_ctx;
} ui_dual_gif_screen_callbacks_t;

typedef void (*ui_dual_gif_preload_progress_cb_t)(uint8_t progress_percent,
                                                  const char *stage,
                                                  void *user_ctx);

void ui_dual_gif_screen_init(void);
void ui_dual_gif_screen_preload_initial_media(void);
void ui_dual_gif_screen_preload_initial_media_with_progress(
    ui_dual_gif_preload_progress_cb_t progress_cb,
    void *user_ctx);
lv_obj_t *ui_dual_gif_screen_root(void);
void ui_dual_gif_screen_destroy(void);
void ui_dual_gif_screen_register_callbacks(const ui_dual_gif_screen_callbacks_t *callbacks);
void ui_dual_gif_screen_set_story_content(const ui_dual_gif_story_content_t *content);
void ui_dual_gif_screen_commit_story_content(void);
void ui_dual_gif_screen_reset_story_state(void);
void ui_dual_gif_screen_start_open(void);
void ui_dual_gif_screen_start_swipe(void);
void ui_dual_gif_screen_start_close(void);
void ui_dual_gif_screen_show_choices(void);
void ui_dual_gif_screen_set_choice_pressed(int choice_index, bool pressed);
void ui_dual_gif_screen_click_choice(int choice_index);
void ui_dual_gif_screen_preload_close(void);
void ui_dual_gif_screen_show_cloud(void);
void ui_dual_gif_screen_set_restart_pressed(bool pressed);
void ui_dual_gif_screen_click_restart(void);
void ui_dual_gif_screen_set_audio_active(bool active);
void ui_dual_gif_screen_set_bird_state(ui_dual_gif_bird_state_t state);
void ui_dual_gif_screen_request_bird_state(ui_dual_gif_bird_state_t state);
void ui_dual_gif_screen_set_bird_talking(bool talking);

#ifdef __cplusplus
}
#endif
