#pragma once

#include <stdbool.h>

#include "story_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    void (*scene_image_ready)(void *user_ctx);
    void (*choice_selected)(int choice_index, void *user_ctx);
    void (*close_finished)(void *user_ctx);
    void (*restart_requested)(void *user_ctx);
    void (*settings_opened)(void *user_ctx);
    void (*settings_closed)(void *user_ctx);
    void *user_ctx;
} story_presenter_callbacks_t;

typedef enum
{
    STORY_PRESENTER_AUDIO_PHASE_IDLE = 0,
    STORY_PRESENTER_AUDIO_PHASE_BUFFERING,
    STORY_PRESENTER_AUDIO_PHASE_PLAYING,
    STORY_PRESENTER_AUDIO_PHASE_CUE,
} story_presenter_audio_phase_t;

void story_presenter_init(const story_presenter_callbacks_t *callbacks);
void story_presenter_reset_state(void);
void story_presenter_reset_story(void);
void story_presenter_start_open(const story_view_model_t *model);
void story_presenter_start_swipe(const story_view_model_t *model);
void story_presenter_start_close(void);
void story_presenter_show_choices(const story_view_model_t *model);
void story_presenter_set_choice_pressed(int choice_index, bool pressed);
void story_presenter_click_choice(int choice_index);
void story_presenter_preload_close(void);
void story_presenter_show_cloud(void);
void story_presenter_set_restart_pressed(bool pressed);
void story_presenter_click_restart(void);
void story_presenter_set_audio_phase(story_presenter_audio_phase_t phase);
void story_presenter_set_talking(bool talking);

#ifdef __cplusplus
}
#endif
