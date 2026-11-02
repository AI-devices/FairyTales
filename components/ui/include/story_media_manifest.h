#pragma once

#include <stdbool.h>

#include "sdkconfig.h"
#include "animation_decoder.h"

#ifndef CONFIG_FAIRYTALE_ANIM_CFA_BOOK
#define CONFIG_FAIRYTALE_ANIM_CFA_BOOK 0
#endif

#ifndef CONFIG_FAIRYTALE_ANIM_CFA_BIRD
#define CONFIG_FAIRYTALE_ANIM_CFA_BIRD 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define STORY_MEDIA_URI_INTRO_NARRATOR          "spiffs:/assets/audio/intro_narrator.mp3"
#define STORY_MEDIA_URI_SCENE_A_NARRATOR_01     "spiffs:/assets/audio/scene_a_narrator_01.mp3"
#define STORY_MEDIA_URI_SCENE_A_NARRATOR_02     "spiffs:/assets/audio/scene_a_narrator_02.mp3"
#define STORY_MEDIA_URI_SCENE_A_MAGIC           "spiffs:/assets/audio/scene_a_magic.pcm"
#define STORY_MEDIA_URI_SCENE_B1_NARRATOR_01    "spiffs:/assets/audio/scene_b1_narrator_01.mp3"
#define STORY_MEDIA_URI_SCENE_B1_1_NARRATOR_01  "spiffs:/assets/audio/scene_b1_1_narrator_01.mp3"
#define STORY_MEDIA_URI_SCENE_B1_1_NARRATOR_02  "spiffs:/assets/audio/scene_b1_1_narrator_02.mp3"
#define STORY_MEDIA_URI_SCENE_B1_1_1_NARRATOR_01 "spiffs:/assets/audio/scene_b1_1_1_narrator_01.mp3"
#define STORY_MEDIA_URI_SCENE_D1_NARRATOR_01    "spiffs:/assets/audio/scene_d1_narrator_01.mp3"
#define STORY_MEDIA_URI_OUTRO_NARRATOR          "spiffs:/assets/audio/outro_narrator.mp3"

#if CONFIG_FAIRYTALE_ANIM_CFA_BOOK
#define STORY_MEDIA_URI_BOOK_OPEN       "sd:/assets/book_open.cfa"
#define STORY_MEDIA_URI_BOOK_SWIPE      "sd:/assets/book_swipe.cfa"
#define STORY_MEDIA_URI_BOOK_CLOSE      "sd:/assets/book_close.cfa"
#else
#define STORY_MEDIA_URI_BOOK_OPEN       "sd:/assets/open-ezgif.gif"
#define STORY_MEDIA_URI_BOOK_SWIPE      "sd:/assets/swipe-ezgif.gif"
#define STORY_MEDIA_URI_BOOK_CLOSE      "sd:/assets/close-ezgif.gif"
#endif

#if CONFIG_FAIRYTALE_ANIM_CFA_BIRD
#define STORY_MEDIA_URI_BIRD_PRE_TALK   "sd:/assets/bird_talk_start.cfa"
#define STORY_MEDIA_URI_BIRD_TALK_LOOP  "sd:/assets/bird_talk.cfa"
#define STORY_MEDIA_URI_BIRD_IDLE       "sd:/assets/bird_idle.cfa"
#else
#define STORY_MEDIA_URI_BIRD_PRE_TALK   "sd:/assets/bird_start_talk.gif"
#define STORY_MEDIA_URI_BIRD_TALK_LOOP  "sd:/assets/bird_talk.gif"
#define STORY_MEDIA_URI_BIRD_IDLE       "sd:/assets/bird_idle.gif"
#endif

typedef enum
{
    STORY_MEDIA_ANIMATION_BOOK_OPEN = 0,
    STORY_MEDIA_ANIMATION_BOOK_SWIPE,
    STORY_MEDIA_ANIMATION_BOOK_CLOSE,
    STORY_MEDIA_ANIMATION_BIRD_PRE_TALK,
    STORY_MEDIA_ANIMATION_BIRD_TALK_LOOP,
    STORY_MEDIA_ANIMATION_BIRD_IDLE,
    STORY_MEDIA_ANIMATION_COUNT,
} story_media_animation_id_t;

typedef enum
{
    STORY_MEDIA_NARRATION_INTRO = 0,
    STORY_MEDIA_NARRATION_SCENE_A_01,
    STORY_MEDIA_NARRATION_SCENE_A_02,
    STORY_MEDIA_NARRATION_SCENE_B1_01,
    STORY_MEDIA_NARRATION_SCENE_B1_1_01,
    STORY_MEDIA_NARRATION_SCENE_B1_1_02,
    STORY_MEDIA_NARRATION_SCENE_B1_1_1_01,
    STORY_MEDIA_NARRATION_SCENE_D1_01,
    STORY_MEDIA_NARRATION_OUTRO,
    STORY_MEDIA_NARRATION_COUNT,
} story_media_narration_id_t;

typedef enum
{
    STORY_MEDIA_SFX_TITLE_SONIC_LOGO = 0,
    STORY_MEDIA_SFX_SUCCESS_CHIME,
    STORY_MEDIA_SFX_VOLUME_RELEASE,
    STORY_MEDIA_SFX_CHOICE_CONFIRM,
    STORY_MEDIA_SFX_REPLAY_CUE,
    STORY_MEDIA_SFX_ACTION_CONFIRM,
    STORY_MEDIA_SFX_ACTION_CANCEL,
    STORY_MEDIA_SFX_CHOICE_SELECT,
    STORY_MEDIA_SFX_CHOICE_BIRD_CHIRP,
    STORY_MEDIA_SFX_STORY_SPARKLE,
    STORY_MEDIA_SFX_QUEEN_MIRROR_LINE_1,
    STORY_MEDIA_SFX_MAGIC_MIRROR_CUE,
    STORY_MEDIA_SFX_QUEEN_MIRROR_LINE_2,
    STORY_MEDIA_SFX_SERVANT_HIDE_LINE,
    STORY_MEDIA_SFX_B1_1_DOOR_OPEN,
    STORY_MEDIA_SFX_B1_1_SNOW_WHITE_LINE,
    STORY_MEDIA_SFX_CHOICE_WOOD_CREAK,
    STORY_MEDIA_SFX_B1_1_1_KITCHEN_TIMER,
    STORY_MEDIA_SFX_B1_1_1_WOODEN_STEPS,
    STORY_MEDIA_SFX_D1_MMM,
    STORY_MEDIA_SFX_D1_CROWD_SING,
    STORY_MEDIA_SFX_COUNT,
} story_media_sfx_id_t;

const char *story_media_animation_uri(story_media_animation_id_t id);
animation_decoder_kind_t story_media_animation_kind(story_media_animation_id_t id);
bool story_media_animation_is_looping(story_media_animation_id_t id);
const char *story_media_sfx_uri(story_media_sfx_id_t id);
const char *story_media_narration_uri(story_media_narration_id_t id);

#ifdef __cplusplus
}
#endif
