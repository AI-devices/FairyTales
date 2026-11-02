#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    STORY_AUDIO_CUE_TITLE_SONIC_LOGO = 0,
    STORY_AUDIO_CUE_SUCCESS_CHIME,
    STORY_AUDIO_CUE_VOLUME_RELEASE,
    STORY_AUDIO_CUE_CHOICE_CONFIRM,
    STORY_AUDIO_CUE_REPLAY_CUE,
    STORY_AUDIO_CUE_ACTION_CONFIRM,
    STORY_AUDIO_CUE_ACTION_CANCEL,
    STORY_AUDIO_CUE_CHOICE_SELECT,
    STORY_AUDIO_CUE_CHOICE_BIRD_CHIRP,
    STORY_AUDIO_CUE_STORY_SPARKLE,
    STORY_AUDIO_CUE_QUEEN_MIRROR_LINE_1,
    STORY_AUDIO_CUE_MAGIC_MIRROR,
    STORY_AUDIO_CUE_QUEEN_MIRROR_LINE_2,
    STORY_AUDIO_CUE_SERVANT_HIDE_LINE,
    STORY_AUDIO_CUE_B1_1_DOOR_OPEN,
    STORY_AUDIO_CUE_B1_1_SNOW_WHITE_LINE,
    STORY_AUDIO_CUE_CHOICE_WOOD_CREAK,
    STORY_AUDIO_CUE_B1_1_1_KITCHEN_TIMER,
    STORY_AUDIO_CUE_B1_1_1_WOODEN_STEPS,
    STORY_AUDIO_CUE_D1_MMM,
    STORY_AUDIO_CUE_D1_CROWD_SING,
} story_audio_cue_t;

typedef void (*story_audio_preload_progress_cb_t)(size_t completed,
                                                  size_t total,
                                                  void *user_ctx);

esp_err_t story_audio_play_cue(story_audio_cue_t cue);
esp_err_t story_audio_fade_out_cues(uint32_t fade_out_ms);
esp_err_t story_audio_preload_narration(story_audio_preload_progress_cb_t progress_cb,
                                        void *user_ctx);

#ifdef __cplusplus
}
#endif
