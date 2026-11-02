#include "story_media_manifest.h"

#include <stddef.h>

static const char *const s_animation_uris[STORY_MEDIA_ANIMATION_COUNT] = {
    [STORY_MEDIA_ANIMATION_BOOK_OPEN] = STORY_MEDIA_URI_BOOK_OPEN,
    [STORY_MEDIA_ANIMATION_BOOK_SWIPE] = STORY_MEDIA_URI_BOOK_SWIPE,
    [STORY_MEDIA_ANIMATION_BOOK_CLOSE] = STORY_MEDIA_URI_BOOK_CLOSE,
    [STORY_MEDIA_ANIMATION_BIRD_PRE_TALK] = STORY_MEDIA_URI_BIRD_PRE_TALK,
    [STORY_MEDIA_ANIMATION_BIRD_TALK_LOOP] = STORY_MEDIA_URI_BIRD_TALK_LOOP,
    [STORY_MEDIA_ANIMATION_BIRD_IDLE] = STORY_MEDIA_URI_BIRD_IDLE,
};

static const char *const s_sfx_uris[STORY_MEDIA_SFX_COUNT] = {
    [STORY_MEDIA_SFX_TITLE_SONIC_LOGO] = "spiffs:/assets/sfx_oneshot/ui/title.wav",
    [STORY_MEDIA_SFX_SUCCESS_CHIME] = "spiffs:/assets/sfx_oneshot/ui/success_chime.wav",
    [STORY_MEDIA_SFX_VOLUME_RELEASE] = "spiffs:/assets/sfx_oneshot/ui/volume_release.wav",
    [STORY_MEDIA_SFX_ACTION_CONFIRM] = "spiffs:/assets/sfx_oneshot/ui/confirm.wav",
    [STORY_MEDIA_SFX_ACTION_CANCEL] = "spiffs:/assets/sfx_oneshot/ui/cancel.wav",
    [STORY_MEDIA_SFX_CHOICE_SELECT] = "spiffs:/assets/sfx_oneshot/ui/select.wav",
    [STORY_MEDIA_SFX_CHOICE_BIRD_CHIRP] = "spiffs:/assets/sfx_oneshot/ambience/bird_chirp.wav",
    [STORY_MEDIA_SFX_STORY_SPARKLE] = "spiffs:/assets/sfx_oneshot/ui/sparkle.wav",
    [STORY_MEDIA_SFX_QUEEN_MIRROR_LINE_1] = "spiffs:/assets/sfx_oneshot/speech/A_queen_1.wav",
    [STORY_MEDIA_SFX_MAGIC_MIRROR_CUE] = "spiffs:/assets/sfx_oneshot/ambience/mirror.wav",
    [STORY_MEDIA_SFX_QUEEN_MIRROR_LINE_2] = "spiffs:/assets/sfx_oneshot/speech/A_queen_2.wav",
    [STORY_MEDIA_SFX_SERVANT_HIDE_LINE] = "spiffs:/assets/sfx_oneshot/speech/A_servant.wav",
    [STORY_MEDIA_SFX_B1_1_DOOR_OPEN] = "spiffs:/assets/sfx_oneshot/ambience/door_open.wav",
    [STORY_MEDIA_SFX_B1_1_SNOW_WHITE_LINE] = "spiffs:/assets/sfx_oneshot/speech/B1_1_snoww.wav",
    [STORY_MEDIA_SFX_CHOICE_WOOD_CREAK] = "spiffs:/assets/sfx_oneshot/ambience/wood_creak.wav",
    [STORY_MEDIA_SFX_B1_1_1_KITCHEN_TIMER] = "spiffs:/assets/sfx_oneshot/ambience/kitchen_timer.wav",
    [STORY_MEDIA_SFX_B1_1_1_WOODEN_STEPS] = "spiffs:/assets/sfx_oneshot/ambience/wooden_steps.wav",
    [STORY_MEDIA_SFX_D1_MMM] = "spiffs:/assets/sfx_oneshot/ambience/mmm.wav",
    [STORY_MEDIA_SFX_D1_CROWD_SING] = "spiffs:/assets/sfx_oneshot/ambience/crowd_sing.wav",
};

static const char *const s_narration_uris[STORY_MEDIA_NARRATION_COUNT] = {
    [STORY_MEDIA_NARRATION_INTRO] = STORY_MEDIA_URI_INTRO_NARRATOR,
    [STORY_MEDIA_NARRATION_SCENE_A_01] = STORY_MEDIA_URI_SCENE_A_NARRATOR_01,
    [STORY_MEDIA_NARRATION_SCENE_A_02] = STORY_MEDIA_URI_SCENE_A_NARRATOR_02,
    [STORY_MEDIA_NARRATION_SCENE_B1_01] = STORY_MEDIA_URI_SCENE_B1_NARRATOR_01,
    [STORY_MEDIA_NARRATION_SCENE_B1_1_01] = STORY_MEDIA_URI_SCENE_B1_1_NARRATOR_01,
    [STORY_MEDIA_NARRATION_SCENE_B1_1_02] = STORY_MEDIA_URI_SCENE_B1_1_NARRATOR_02,
    [STORY_MEDIA_NARRATION_SCENE_B1_1_1_01] = STORY_MEDIA_URI_SCENE_B1_1_1_NARRATOR_01,
    [STORY_MEDIA_NARRATION_SCENE_D1_01] = STORY_MEDIA_URI_SCENE_D1_NARRATOR_01,
    [STORY_MEDIA_NARRATION_OUTRO] = STORY_MEDIA_URI_OUTRO_NARRATOR,
};

const char *story_media_animation_uri(story_media_animation_id_t id)
{
    if (id < 0 || id >= STORY_MEDIA_ANIMATION_COUNT) {
        return NULL;
    }

    return s_animation_uris[id];
}

animation_decoder_kind_t story_media_animation_kind(story_media_animation_id_t id)
{
    switch (id) {
    case STORY_MEDIA_ANIMATION_BOOK_OPEN:
    case STORY_MEDIA_ANIMATION_BOOK_SWIPE:
    case STORY_MEDIA_ANIMATION_BOOK_CLOSE:
#if CONFIG_FAIRYTALE_ANIM_CFA_BOOK
        return ANIMATION_DECODER_KIND_CFA;
#else
        return ANIMATION_DECODER_KIND_GIF;
#endif

    case STORY_MEDIA_ANIMATION_BIRD_PRE_TALK:
    case STORY_MEDIA_ANIMATION_BIRD_TALK_LOOP:
    case STORY_MEDIA_ANIMATION_BIRD_IDLE:
#if CONFIG_FAIRYTALE_ANIM_CFA_BIRD
        return ANIMATION_DECODER_KIND_CFA;
#else
        return ANIMATION_DECODER_KIND_GIF;
#endif

    default:
        break;
    }

    return ANIMATION_DECODER_KIND_GIF;
}

bool story_media_animation_is_looping(story_media_animation_id_t id)
{
    switch (id) {
    case STORY_MEDIA_ANIMATION_BIRD_TALK_LOOP:
    case STORY_MEDIA_ANIMATION_BIRD_IDLE:
        return true;

    case STORY_MEDIA_ANIMATION_BOOK_OPEN:
    case STORY_MEDIA_ANIMATION_BOOK_SWIPE:
    case STORY_MEDIA_ANIMATION_BOOK_CLOSE:
    case STORY_MEDIA_ANIMATION_BIRD_PRE_TALK:
    default:
        return false;
    }
}

const char *story_media_sfx_uri(story_media_sfx_id_t id)
{
    if (id < 0 || id >= STORY_MEDIA_SFX_COUNT) {
        return NULL;
    }

    return s_sfx_uris[id];
}

const char *story_media_narration_uri(story_media_narration_id_t id)
{
    if (id < 0 || id >= STORY_MEDIA_NARRATION_COUNT) {
        return NULL;
    }
    return s_narration_uris[id];
}
