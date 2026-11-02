#include "story_audio.h"

#include "audio_session.h"
#include "esp_log.h"
#include "story_media_manifest.h"

static const char *TAG = "story_audio";

typedef struct
{
    story_media_sfx_id_t sfx_id;
    int volume_percent;
    uint32_t fade_out_delay_ms;
    uint32_t fade_out_ms;
} story_audio_cue_desc_t;

static const story_audio_cue_desc_t s_cue_descs[] = {
    [STORY_AUDIO_CUE_TITLE_SONIC_LOGO] = {
        .sfx_id = STORY_MEDIA_SFX_TITLE_SONIC_LOGO,
        .volume_percent = 50,
    },
    [STORY_AUDIO_CUE_SUCCESS_CHIME] = {
        .sfx_id = STORY_MEDIA_SFX_SUCCESS_CHIME,
        .volume_percent = 40,
    },
    [STORY_AUDIO_CUE_VOLUME_RELEASE] = {
        .sfx_id = STORY_MEDIA_SFX_VOLUME_RELEASE,
        .volume_percent = 40,
    },
    [STORY_AUDIO_CUE_CHOICE_CONFIRM] = {
        .sfx_id = STORY_MEDIA_SFX_CHOICE_CONFIRM,
        .volume_percent = 44,
    },
    [STORY_AUDIO_CUE_REPLAY_CUE] = {
        .sfx_id = STORY_MEDIA_SFX_REPLAY_CUE,
        .volume_percent = 46,
    },
    [STORY_AUDIO_CUE_ACTION_CONFIRM] = {
        .sfx_id = STORY_MEDIA_SFX_ACTION_CONFIRM,
        .volume_percent = 90,
    },
    [STORY_AUDIO_CUE_ACTION_CANCEL] = {
        .sfx_id = STORY_MEDIA_SFX_ACTION_CANCEL,
        .volume_percent = 100,
    },
    [STORY_AUDIO_CUE_CHOICE_SELECT] = {
        .sfx_id = STORY_MEDIA_SFX_CHOICE_SELECT,
        .volume_percent = 30,
    },
    [STORY_AUDIO_CUE_CHOICE_BIRD_CHIRP] = {
        .sfx_id = STORY_MEDIA_SFX_CHOICE_BIRD_CHIRP,
        .volume_percent = 55,
    },
    [STORY_AUDIO_CUE_STORY_SPARKLE] = {
        .sfx_id = STORY_MEDIA_SFX_STORY_SPARKLE,
        .volume_percent = 10,
    },
    [STORY_AUDIO_CUE_QUEEN_MIRROR_LINE_1] = {
        .sfx_id = STORY_MEDIA_SFX_QUEEN_MIRROR_LINE_1,
        .volume_percent = 100,
    },
    [STORY_AUDIO_CUE_MAGIC_MIRROR] = {
        .sfx_id = STORY_MEDIA_SFX_MAGIC_MIRROR_CUE,
        .volume_percent = 30,
    },
    [STORY_AUDIO_CUE_QUEEN_MIRROR_LINE_2] = {
        .sfx_id = STORY_MEDIA_SFX_QUEEN_MIRROR_LINE_2,
        .volume_percent = 100,
    },
    [STORY_AUDIO_CUE_SERVANT_HIDE_LINE] = {
        .sfx_id = STORY_MEDIA_SFX_SERVANT_HIDE_LINE,
        .volume_percent = 90,
    },
    [STORY_AUDIO_CUE_B1_1_DOOR_OPEN] = {
        .sfx_id = STORY_MEDIA_SFX_B1_1_DOOR_OPEN,
        .volume_percent = 90,
    },
    [STORY_AUDIO_CUE_B1_1_SNOW_WHITE_LINE] = {
        .sfx_id = STORY_MEDIA_SFX_B1_1_SNOW_WHITE_LINE,
        .volume_percent = 100,
    },
    [STORY_AUDIO_CUE_CHOICE_WOOD_CREAK] = {
        .sfx_id = STORY_MEDIA_SFX_CHOICE_WOOD_CREAK,
        .volume_percent = 55,
    },
    [STORY_AUDIO_CUE_B1_1_1_KITCHEN_TIMER] = {
        .sfx_id = STORY_MEDIA_SFX_B1_1_1_KITCHEN_TIMER,
        .volume_percent = 70,
    },
    [STORY_AUDIO_CUE_B1_1_1_WOODEN_STEPS] = {
        .sfx_id = STORY_MEDIA_SFX_B1_1_1_WOODEN_STEPS,
        .volume_percent = 70,
    },
    [STORY_AUDIO_CUE_D1_MMM] = {
        .sfx_id = STORY_MEDIA_SFX_D1_MMM,
        .volume_percent = 100,
    },
    [STORY_AUDIO_CUE_D1_CROWD_SING] = {
        .sfx_id = STORY_MEDIA_SFX_D1_CROWD_SING,
        .volume_percent = 30,
    },
};

esp_err_t story_audio_play_cue(story_audio_cue_t cue)
{
    const char *uri = NULL;

    if ((unsigned)cue >= (sizeof(s_cue_descs) / sizeof(s_cue_descs[0]))) {
        return ESP_ERR_INVALID_ARG;
    }

    uri = story_media_sfx_uri(s_cue_descs[cue].sfx_id);
    if (!uri || uri[0] == '\0') {
        return ESP_OK;
    }

    if (s_cue_descs[cue].fade_out_ms > 0) {
        return audio_session_play_sfx_uri_with_fade_out(uri,
                                                        s_cue_descs[cue].volume_percent,
                                                        s_cue_descs[cue].fade_out_delay_ms,
                                                        s_cue_descs[cue].fade_out_ms);
    }

    return audio_session_play_sfx_uri(uri, s_cue_descs[cue].volume_percent);
}

esp_err_t story_audio_fade_out_cues(uint32_t fade_out_ms)
{
    return audio_session_fade_out_sfx(fade_out_ms);
}

esp_err_t story_audio_preload_narration(story_audio_preload_progress_cb_t progress_cb,
                                        void *user_ctx)
{
    esp_err_t first_error = ESP_OK;

    for (int i = 0; i < STORY_MEDIA_NARRATION_COUNT; ++i) {
        const char *uri = story_media_narration_uri((story_media_narration_id_t)i);
        if (!uri || uri[0] == '\0') {
            if (progress_cb) {
                progress_cb((size_t)i + 1U, STORY_MEDIA_NARRATION_COUNT, user_ctx);
            }
            continue;
        }

        const esp_err_t err = audio_session_preload_uri(uri);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "narration preload skipped uri=%s error=%s",
                     uri,
                     esp_err_to_name(err));
            if (first_error == ESP_OK) {
                first_error = err;
            }
        }
        if (progress_cb) {
            progress_cb((size_t)i + 1U, STORY_MEDIA_NARRATION_COUNT, user_ctx);
        }
    }

    return first_error;
}
