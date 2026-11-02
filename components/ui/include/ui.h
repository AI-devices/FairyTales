#pragma once

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

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);
void ui_destroy(void);
void ui_request_restart_to_title(void);
void ui_story_bird_talk_anim_start(void);
void ui_story_bird_talk_anim_stop(void);
void ui_notify_tts_finished(void);
void ui_notify_audio_playback_finished(void);

#ifdef __cplusplus
}
#endif
