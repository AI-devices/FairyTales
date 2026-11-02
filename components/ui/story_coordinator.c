#include "story_coordinator.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"

#include "story_audio.h"
#include "story_media_manifest.h"
#include "story_presenter.h"
#include "ui.h"
#include "ui_dispatch.h"

#define STORY_INTRO_DELAY_MS 2000U
#define STORY_OUTRO_DELAY_MS 1000U
#define STORY_LOOP_CHOICE_DELAY_MS 10000U
#define STORY_LOOP_RESTART_DELAY_MS 5000U
#define STORY_CHOICE_CUE_FIRST_DELAY_MS 250U
#define STORY_CHOICE_CUE_DURATION_MS 2100U
#define STORY_CHOICE_WOOD_CREAK_DURATION_MS 1520U
#define STORY_CHOICE_CUE_SECOND_PAUSE_MS 5000U
#define STORY_CHOICE_CUE_RANDOM_MIN_PAUSE_MS 7000U
#define STORY_CHOICE_CUE_RANDOM_MAX_PAUSE_MS 12000U
#define STORY_NODE_B1_1_1_WOODEN_STEPS_DELAY_MS 2600U
#define STORY_NODE_D1_MMM_DELAY_MS 1600U
#define STORY_NODE_D1_CROWD_SING_DELAY_MS 12000U
#define STORY_NAVIGATION_AUDIO_FADE_MS 250U

typedef enum
{
    STORY_AUDIO_FLOW_NONE = 0,
    STORY_AUDIO_FLOW_INTRO,
    STORY_AUDIO_FLOW_SCENE,
    STORY_AUDIO_FLOW_OUTRO,
} story_audio_flow_t;

typedef struct
{
    const char *uri;
    bool narrator;
} story_audio_clip_t;

typedef struct
{
    const story_audio_clip_t *clips;
    size_t count;
} story_audio_sequence_t;

static const char *TAG = "story_coord";
static const uint32_t STORY_AUTO_CHOICE_PRESS_MS = 180U;
static const uint32_t STORY_AUTO_RESTART_PRESS_MS = 80U;

static const story_audio_clip_t s_intro_audio[] = {
    {STORY_MEDIA_URI_INTRO_NARRATOR, true},
};

static const story_audio_clip_t s_scene_a_audio[] = {
    {STORY_MEDIA_URI_SCENE_A_NARRATOR_01, true},
    {"spiffs:/assets/sfx_oneshot/speech/A_queen_1.wav", false},
    {"spiffs:/assets/sfx_oneshot/ambience/mirror.wav", false},
    {STORY_MEDIA_URI_SCENE_A_MAGIC, false},
    {STORY_MEDIA_URI_SCENE_A_NARRATOR_02, true},
    {"spiffs:/assets/sfx_oneshot/speech/A_servant.wav", false},
};

static const story_audio_clip_t s_scene_b1_audio[] = {
    {STORY_MEDIA_URI_SCENE_B1_NARRATOR_01, true},
};

static const story_audio_clip_t s_scene_b1_1_audio[] = {
    {"spiffs:/assets/sfx_oneshot/ambience/door_open.wav", false},
    {STORY_MEDIA_URI_SCENE_B1_1_NARRATOR_01, true},
    {"spiffs:/assets/sfx_oneshot/speech/B1_1_snoww.wav", false},
    {STORY_MEDIA_URI_SCENE_B1_1_NARRATOR_02, true},
};

static const story_audio_clip_t s_scene_b1_1_1_audio[] = {
    {STORY_MEDIA_URI_SCENE_B1_1_1_NARRATOR_01, true},
};

static const story_audio_clip_t s_scene_d1_audio[] = {
    {STORY_MEDIA_URI_SCENE_D1_NARRATOR_01, true},
};

static const story_audio_clip_t s_outro_audio[] = {
    {STORY_MEDIA_URI_OUTRO_NARRATOR, true},
};

static story_engine_t s_engine;
static story_app_mode_t s_mode = STORY_APP_MODE_LOOP;
static story_audio_flow_t s_audio_flow = STORY_AUDIO_FLOW_NONE;
static story_audio_sequence_t s_audio_sequence = {0};
static size_t s_audio_index = 0;
static uint32_t s_active_audio_session_id = 0;
static bool s_started = false;
static bool s_next_loop_choice_is_play_again = true;
static int s_auto_choice_pending_index = 0;
static bool s_auto_restart_press_pending = false;
static bool s_settings_open = false;
static story_phase_t s_settings_resume_phase = STORY_PHASE_SCENE;

static lv_timer_t *s_intro_timer = NULL;
static lv_timer_t *s_outro_timer = NULL;
static lv_timer_t *s_auto_choice_timer = NULL;
static lv_timer_t *s_auto_restart_timer = NULL;
static lv_timer_t *s_choice_cue_timer = NULL;
static lv_timer_t *s_scene_cue_timer = NULL;
static lv_timer_t *s_navigation_audio_stop_timer = NULL;
static unsigned s_scene_cue_stage = 0;
static uint32_t s_choice_cue_count = 0;

static void start_scene_audio(void);
static void finish_scene_audio(void);
static void show_choices(void);
static void start_outro(void);
static void restart_from_title(void);
static void auto_choice_commit_timer_cb(lv_timer_t *timer);
static void auto_restart_timer_cb(lv_timer_t *timer);
static void auto_restart_commit_timer_cb(lv_timer_t *timer);
static void arm_choice_timers(void);

static void cancel_timer(lv_timer_t **timer)
{
    if (timer && *timer) {
        lv_timer_del(*timer);
        *timer = NULL;
    }
}

static void clear_auto_choice_feedback(void)
{
    if (s_auto_choice_pending_index != 0) {
        story_presenter_set_choice_pressed(s_auto_choice_pending_index, false);
        s_auto_choice_pending_index = 0;
    }
}

static void clear_auto_restart_feedback(void)
{
    if (s_auto_restart_press_pending) {
        story_presenter_set_restart_pressed(false);
        s_auto_restart_press_pending = false;
    }
}

static void cancel_all_timers(void)
{
    cancel_timer(&s_intro_timer);
    cancel_timer(&s_outro_timer);
    cancel_timer(&s_auto_choice_timer);
    cancel_timer(&s_auto_restart_timer);
    cancel_timer(&s_choice_cue_timer);
    cancel_timer(&s_scene_cue_timer);
    cancel_timer(&s_navigation_audio_stop_timer);
    clear_auto_choice_feedback();
    clear_auto_restart_feedback();
    s_scene_cue_stage = 0;
    s_choice_cue_count = 0;
}

static void stop_audio(void)
{
    cancel_timer(&s_navigation_audio_stop_timer);
    s_active_audio_session_id = 0;
    s_audio_flow = STORY_AUDIO_FLOW_NONE;
    memset(&s_audio_sequence, 0, sizeof(s_audio_sequence));
    s_audio_index = 0;
    (void)audio_session_stop();
    story_presenter_set_audio_phase(STORY_PRESENTER_AUDIO_PHASE_IDLE);
}

static void navigation_audio_stop_timer_cb(lv_timer_t *timer)
{
    if (s_navigation_audio_stop_timer == timer) {
        s_navigation_audio_stop_timer = NULL;
    }
    (void)audio_session_stop();
}

static void stop_audio_smoothly(void)
{
    cancel_timer(&s_navigation_audio_stop_timer);
    s_active_audio_session_id = 0;
    s_audio_flow = STORY_AUDIO_FLOW_NONE;
    memset(&s_audio_sequence, 0, sizeof(s_audio_sequence));
    s_audio_index = 0;
    story_presenter_set_audio_phase(STORY_PRESENTER_AUDIO_PHASE_IDLE);

    (void)audio_session_fade_out_primary(STORY_NAVIGATION_AUDIO_FADE_MS);
    (void)audio_session_fade_out_sfx(STORY_NAVIGATION_AUDIO_FADE_MS);
    s_navigation_audio_stop_timer = lv_timer_create(navigation_audio_stop_timer_cb,
                                                     STORY_NAVIGATION_AUDIO_FADE_MS,
                                                     NULL);
    if (s_navigation_audio_stop_timer) {
        lv_timer_set_repeat_count(s_navigation_audio_stop_timer, 1);
    } else {
        (void)audio_session_stop();
    }
}

static void finish_navigation_audio_stop(void)
{
    cancel_timer(&s_navigation_audio_stop_timer);
    (void)audio_session_stop();
}

static uint32_t random_range(uint32_t minimum, uint32_t maximum)
{
    if (maximum <= minimum) {
        return minimum;
    }
    return minimum + (esp_random() % (maximum - minimum + 1U));
}

static void refresh_model(story_view_model_t *model)
{
    story_engine_build_view_model(&s_engine, model);
    if (!model) {
        return;
    }

    if (s_mode == STORY_APP_MODE_LOOP) {
        /* Loop actions are not edges in the fairy-tale graph. */
        model->node = NULL;
        model->question_text = "What would you like to do?";
        model->choice1_text = "Play again";
        model->choice2_text = "Finish story";
        model->choice2_enabled = true;
        model->is_final = false;
    } else if (!model->is_final) {
        model->choice2_enabled = false;
    }
}

static story_audio_sequence_t scene_sequence_for_case(builtin_text_case_t case_id)
{
    switch (case_id) {
    case CASE_TXT_01:
        return (story_audio_sequence_t){s_scene_a_audio, sizeof(s_scene_a_audio) / sizeof(s_scene_a_audio[0])};
    case CASE_TXT_02:
        return (story_audio_sequence_t){s_scene_b1_audio, sizeof(s_scene_b1_audio) / sizeof(s_scene_b1_audio[0])};
    case CASE_TXT_04:
        return (story_audio_sequence_t){s_scene_b1_1_audio, sizeof(s_scene_b1_1_audio) / sizeof(s_scene_b1_1_audio[0])};
    case CASE_TXT_08:
        return (story_audio_sequence_t){s_scene_b1_1_1_audio, sizeof(s_scene_b1_1_1_audio) / sizeof(s_scene_b1_1_1_audio[0])};
    case CASE_TXT_16:
        return (story_audio_sequence_t){s_scene_d1_audio, sizeof(s_scene_d1_audio) / sizeof(s_scene_d1_audio[0])};
    default:
        return (story_audio_sequence_t){0};
    }
}

static void audio_sequence_finished(void)
{
    story_audio_flow_t flow = s_audio_flow;

    s_active_audio_session_id = 0;
    s_audio_flow = STORY_AUDIO_FLOW_NONE;
    memset(&s_audio_sequence, 0, sizeof(s_audio_sequence));
    s_audio_index = 0;
    story_presenter_set_audio_phase(STORY_PRESENTER_AUDIO_PHASE_IDLE);

    switch (flow) {
    case STORY_AUDIO_FLOW_INTRO: {
        story_view_model_t model = {0};
        story_engine_set_phase(&s_engine, STORY_PHASE_SCENE);
        refresh_model(&model);
        story_presenter_start_open(&model);
        break;
    }
    case STORY_AUDIO_FLOW_SCENE:
        finish_scene_audio();
        break;
    case STORY_AUDIO_FLOW_OUTRO:
        story_engine_set_phase(&s_engine, STORY_PHASE_COMPLETE);
        story_presenter_show_cloud();
        if (s_mode == STORY_APP_MODE_LOOP) {
            s_auto_restart_timer = lv_timer_create(auto_restart_timer_cb,
                                                   STORY_LOOP_RESTART_DELAY_MS,
                                                   NULL);
            if (s_auto_restart_timer) {
                lv_timer_set_repeat_count(s_auto_restart_timer, 1);
            }
        }
        break;
    case STORY_AUDIO_FLOW_NONE:
    default:
        break;
    }
}

static void start_current_audio_clip(void)
{
    if (!s_audio_sequence.clips || s_audio_index >= s_audio_sequence.count) {
        audio_sequence_finished();
        return;
    }

    const story_audio_clip_t *clip = &s_audio_sequence.clips[s_audio_index];
    uint32_t session_id = 0;
    story_presenter_set_audio_phase(clip->narrator ? STORY_PRESENTER_AUDIO_PHASE_BUFFERING
                                                  : STORY_PRESENTER_AUDIO_PHASE_CUE);
    esp_err_t err = audio_session_start_uri(clip->uri, &session_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "unable to queue audio %s: %s", clip->uri, esp_err_to_name(err));
        ++s_audio_index;
        start_current_audio_clip();
        return;
    }
    s_active_audio_session_id = session_id;
}

static void start_audio_sequence(story_audio_flow_t flow, story_audio_sequence_t sequence)
{
    s_audio_flow = flow;
    s_audio_sequence = sequence;
    s_audio_index = 0;
    s_active_audio_session_id = 0;
    start_current_audio_clip();
}

static void scene_cue_timer_cb(lv_timer_t *timer)
{
    if (s_scene_cue_timer == timer) {
        s_scene_cue_timer = NULL;
    }

    if (s_engine.state.phase != STORY_PHASE_SCENE) {
        return;
    }

    if (s_engine.state.current_case == CASE_TXT_08) {
        (void)story_audio_play_cue(STORY_AUDIO_CUE_B1_1_1_WOODEN_STEPS);
        return;
    }

    if (s_engine.state.current_case == CASE_TXT_16) {
        if (s_scene_cue_stage == 1U) {
            (void)story_audio_play_cue(STORY_AUDIO_CUE_D1_MMM);
            s_scene_cue_stage = 2U;
            s_scene_cue_timer = lv_timer_create(scene_cue_timer_cb,
                                                STORY_NODE_D1_CROWD_SING_DELAY_MS,
                                                NULL);
            if (s_scene_cue_timer) {
                lv_timer_set_repeat_count(s_scene_cue_timer, 1);
            }
        } else if (s_scene_cue_stage == 2U) {
            (void)story_audio_play_cue(STORY_AUDIO_CUE_D1_CROWD_SING);
            s_scene_cue_stage = 0U;
        }
    }
}

static void schedule_scene_cues(void)
{
    cancel_timer(&s_scene_cue_timer);
    s_scene_cue_stage = 0;

    uint32_t delay = 0;
    if (s_engine.state.current_case == CASE_TXT_08) {
        delay = STORY_NODE_B1_1_1_WOODEN_STEPS_DELAY_MS;
    } else if (s_engine.state.current_case == CASE_TXT_16) {
        s_scene_cue_stage = 1U;
        delay = STORY_NODE_D1_MMM_DELAY_MS;
    }

    if (delay > 0) {
        s_scene_cue_timer = lv_timer_create(scene_cue_timer_cb, delay, NULL);
        if (s_scene_cue_timer) {
            lv_timer_set_repeat_count(s_scene_cue_timer, 1);
        }
    }
}

static void start_scene_audio(void)
{
    story_audio_sequence_t sequence = scene_sequence_for_case(s_engine.state.current_case);
    if (s_engine.state.current_case == CASE_TXT_02) {
        (void)story_audio_play_cue(STORY_AUDIO_CUE_CHOICE_BIRD_CHIRP);
    } else if (s_engine.state.current_case == CASE_TXT_08) {
        (void)story_audio_play_cue(STORY_AUDIO_CUE_B1_1_1_KITCHEN_TIMER);
    }
    schedule_scene_cues();
    start_audio_sequence(STORY_AUDIO_FLOW_SCENE, sequence);
}

static void choice_cue_timer_cb(lv_timer_t *timer)
{
    if (s_choice_cue_timer == timer) {
        s_choice_cue_timer = NULL;
    }
    if (s_engine.state.phase != STORY_PHASE_CHOICE) {
        return;
    }

    bool use_wood_creak = s_engine.state.current_case == CASE_TXT_04 ||
                          s_engine.state.current_case == CASE_TXT_08;
    (void)story_audio_play_cue(use_wood_creak
                                  ? STORY_AUDIO_CUE_CHOICE_WOOD_CREAK
                                  : STORY_AUDIO_CUE_CHOICE_BIRD_CHIRP);

    ++s_choice_cue_count;
    uint32_t pause = s_choice_cue_count == 1U
                         ? STORY_CHOICE_CUE_SECOND_PAUSE_MS
                         : random_range(STORY_CHOICE_CUE_RANDOM_MIN_PAUSE_MS,
                                        STORY_CHOICE_CUE_RANDOM_MAX_PAUSE_MS);
    s_choice_cue_timer = lv_timer_create(choice_cue_timer_cb,
                                         (use_wood_creak
                                              ? STORY_CHOICE_WOOD_CREAK_DURATION_MS
                                              : STORY_CHOICE_CUE_DURATION_MS) + pause,
                                         NULL);
    if (s_choice_cue_timer) {
        lv_timer_set_repeat_count(s_choice_cue_timer, 1);
    }
}

static void perform_choice(int choice_index)
{
    if (s_engine.state.phase != STORY_PHASE_CHOICE) {
        return;
    }

    cancel_timer(&s_auto_choice_timer);
    clear_auto_choice_feedback();
    cancel_timer(&s_choice_cue_timer);
    cancel_timer(&s_scene_cue_timer);
    stop_audio();

    if (s_mode == STORY_APP_MODE_LOOP) {
        if (choice_index == 1) {
            story_view_model_t model = {0};
            s_next_loop_choice_is_play_again = false;
            story_engine_set_phase(&s_engine, STORY_PHASE_SCENE);
            refresh_model(&model);
            story_presenter_start_swipe(&model);
        } else {
            s_next_loop_choice_is_play_again = true;
            story_engine_set_phase(&s_engine, STORY_PHASE_OUTRO);
            story_presenter_start_close();
        }
        return;
    }

    if (choice_index != 1) {
        return;
    }

    story_action_t action = {.type = STORY_ACTION_CHOOSE_1};
    story_view_model_t model = {0};
    story_engine_apply(&s_engine, &action);
    story_engine_set_phase(&s_engine, STORY_PHASE_SCENE);
    refresh_model(&model);
    story_presenter_start_swipe(&model);
}

static void auto_choice_timer_cb(lv_timer_t *timer)
{
    int choice_index = 0;

    if (s_auto_choice_timer == timer) {
        s_auto_choice_timer = NULL;
    }
    if (s_engine.state.phase != STORY_PHASE_CHOICE) {
        return;
    }

    cancel_timer(&s_choice_cue_timer);
    choice_index = s_next_loop_choice_is_play_again ? 1 : 2;
    s_auto_choice_pending_index = choice_index;
    story_presenter_set_choice_pressed(choice_index, true);

    s_auto_choice_timer = lv_timer_create(auto_choice_commit_timer_cb,
                                          STORY_AUTO_CHOICE_PRESS_MS,
                                          NULL);
    if (s_auto_choice_timer) {
        lv_timer_set_repeat_count(s_auto_choice_timer, 1);
    } else {
        s_auto_choice_pending_index = 0;
        story_presenter_click_choice(choice_index);
    }
}

static void auto_choice_commit_timer_cb(lv_timer_t *timer)
{
    const int choice_index = s_auto_choice_pending_index;

    if (s_auto_choice_timer == timer) {
        s_auto_choice_timer = NULL;
    }
    s_auto_choice_pending_index = 0;
    if (choice_index != 0) {
        story_presenter_click_choice(choice_index);
    }
}

static void auto_restart_timer_cb(lv_timer_t *timer)
{
    if (s_auto_restart_timer == timer) {
        s_auto_restart_timer = NULL;
    }

    if (s_engine.state.phase != STORY_PHASE_COMPLETE) {
        return;
    }

    s_auto_restart_press_pending = true;
    story_presenter_set_restart_pressed(true);
    s_auto_restart_timer = lv_timer_create(auto_restart_commit_timer_cb,
                                           STORY_AUTO_RESTART_PRESS_MS,
                                           NULL);
    if (s_auto_restart_timer) {
        lv_timer_set_repeat_count(s_auto_restart_timer, 1);
    } else {
        s_auto_restart_press_pending = false;
        story_presenter_click_restart();
    }
}

static void auto_restart_commit_timer_cb(lv_timer_t *timer)
{
    if (s_auto_restart_timer == timer) {
        s_auto_restart_timer = NULL;
    }
    if (!s_auto_restart_press_pending) {
        return;
    }

    s_auto_restart_press_pending = false;
    story_presenter_click_restart();
}

static void show_choices(void)
{
    story_view_model_t model = {0};

    story_engine_set_phase(&s_engine, STORY_PHASE_CHOICE);
    refresh_model(&model);
    story_presenter_show_choices(&model);

    if (s_mode == STORY_APP_MODE_LOOP) {
        story_presenter_preload_close();
    }

    arm_choice_timers();
}

static void arm_choice_timers(void)
{
    cancel_timer(&s_auto_choice_timer);
    cancel_timer(&s_choice_cue_timer);
    clear_auto_choice_feedback();

    s_choice_cue_count = 0;
    s_choice_cue_timer = lv_timer_create(choice_cue_timer_cb,
                                         STORY_CHOICE_CUE_FIRST_DELAY_MS,
                                         NULL);
    if (s_choice_cue_timer) {
        lv_timer_set_repeat_count(s_choice_cue_timer, 1);
    }

    if (s_mode == STORY_APP_MODE_LOOP) {
        s_auto_choice_timer = lv_timer_create(auto_choice_timer_cb,
                                              STORY_LOOP_CHOICE_DELAY_MS,
                                              NULL);
        if (s_auto_choice_timer) {
            lv_timer_set_repeat_count(s_auto_choice_timer, 1);
        }
    }
}

static void finish_scene_audio(void)
{
    cancel_timer(&s_scene_cue_timer);
    const story_node_t *node = story_get_node(s_engine.state.current_case);
    if (s_mode == STORY_APP_MODE_FULL && node && node->is_final) {
        story_engine_set_phase(&s_engine, STORY_PHASE_OUTRO);
        story_presenter_start_close();
    } else {
        show_choices();
    }
}

static void outro_timer_cb(lv_timer_t *timer)
{
    if (s_outro_timer == timer) {
        s_outro_timer = NULL;
    }
    start_outro();
}

static void start_outro(void)
{
    start_audio_sequence(STORY_AUDIO_FLOW_OUTRO,
                         (story_audio_sequence_t){s_outro_audio,
                                                  sizeof(s_outro_audio) / sizeof(s_outro_audio[0])});
}

static void intro_timer_cb(lv_timer_t *timer)
{
    if (s_intro_timer == timer) {
        s_intro_timer = NULL;
    }
    story_engine_set_phase(&s_engine, STORY_PHASE_INTRO);
    start_audio_sequence(STORY_AUDIO_FLOW_INTRO,
                         (story_audio_sequence_t){s_intro_audio,
                                                  sizeof(s_intro_audio) / sizeof(s_intro_audio[0])});
}

static void restart_from_title(void)
{
    cancel_all_timers();
    stop_audio();
    ui_request_restart_to_title();
}

static void scene_image_ready(void *user_ctx)
{
    (void)user_ctx;
    story_engine_set_phase(&s_engine, STORY_PHASE_SCENE);
    start_scene_audio();
}

static void choice_selected(int choice_index, void *user_ctx)
{
    (void)user_ctx;
    perform_choice(choice_index);
}

static void close_finished(void *user_ctx)
{
    (void)user_ctx;
    cancel_timer(&s_outro_timer);
    s_outro_timer = lv_timer_create(outro_timer_cb, STORY_OUTRO_DELAY_MS, NULL);
    if (s_outro_timer) {
        lv_timer_set_repeat_count(s_outro_timer, 1);
    } else {
        start_outro();
    }
}

static void restart_requested(void *user_ctx)
{
    (void)user_ctx;
    restart_from_title();
}

static void settings_opened(void *user_ctx)
{
    (void)user_ctx;

    if (s_settings_open) {
        return;
    }

    s_settings_open = true;
    s_settings_resume_phase = s_engine.state.phase;
    cancel_all_timers();
    stop_audio_smoothly();
}

static void settings_closed(void *user_ctx)
{
    (void)user_ctx;

    if (!s_settings_open) {
        return;
    }

    s_settings_open = false;
    finish_navigation_audio_stop();

    switch (s_settings_resume_phase) {
    case STORY_PHASE_INTRO:
        story_engine_set_phase(&s_engine, STORY_PHASE_INTRO);
        start_audio_sequence(STORY_AUDIO_FLOW_INTRO,
                             (story_audio_sequence_t){s_intro_audio,
                                                      sizeof(s_intro_audio) / sizeof(s_intro_audio[0])});
        break;
    case STORY_PHASE_SCENE:
        story_engine_set_phase(&s_engine, STORY_PHASE_SCENE);
        start_scene_audio();
        break;
    case STORY_PHASE_CHOICE:
        story_engine_set_phase(&s_engine, STORY_PHASE_CHOICE);
        arm_choice_timers();
        break;
    case STORY_PHASE_COMPLETE:
        if (s_mode == STORY_APP_MODE_LOOP) {
            s_auto_restart_timer = lv_timer_create(auto_restart_timer_cb,
                                                   STORY_LOOP_RESTART_DELAY_MS,
                                                   NULL);
            if (s_auto_restart_timer) {
                lv_timer_set_repeat_count(s_auto_restart_timer, 1);
            }
        }
        break;
    case STORY_PHASE_OUTRO:
        story_engine_set_phase(&s_engine, STORY_PHASE_OUTRO);
        start_outro();
        break;
    default:
        break;
    }
}

void story_coordinator_init(void)
{
    const story_presenter_callbacks_t callbacks = {
        .scene_image_ready = scene_image_ready,
        .choice_selected = choice_selected,
        .close_finished = close_finished,
        .restart_requested = restart_requested,
        .settings_opened = settings_opened,
        .settings_closed = settings_closed,
        .user_ctx = NULL,
    };

    s_mode = STORY_APP_MODE_LOOP;
    s_next_loop_choice_is_play_again = true;
    s_settings_open = false;
    story_engine_init(&s_engine);
    story_presenter_init(&callbacks);
    cancel_all_timers();
    stop_audio();
    s_started = false;
}

void story_coordinator_reset(void)
{
    cancel_all_timers();
    stop_audio();
    story_engine_init(&s_engine);
    story_presenter_reset_state();
    s_settings_open = false;
    s_started = false;
}

void story_coordinator_start(void)
{
    if (s_started) {
        return;
    }

    s_started = true;
    story_engine_init(&s_engine);
    story_presenter_reset_story();
    s_intro_timer = lv_timer_create(intro_timer_cb, STORY_INTRO_DELAY_MS, NULL);
    if (s_intro_timer) {
        lv_timer_set_repeat_count(s_intro_timer, 1);
    } else {
        intro_timer_cb(NULL);
    }
}

bool story_coordinator_is_action_allowed(story_action_type_t action_type)
{
    if (action_type == STORY_ACTION_CHOOSE_2 && s_mode == STORY_APP_MODE_FULL) {
        return false;
    }
    if (action_type == STORY_ACTION_CHOOSE_1 || action_type == STORY_ACTION_CHOOSE_2) {
        return s_engine.state.phase == STORY_PHASE_CHOICE;
    }
    return true;
}

void story_coordinator_handle_action(const story_action_t *action)
{
    if (!action) {
        return;
    }

    if (action->type == STORY_ACTION_CHOOSE_1 || action->type == STORY_ACTION_CHOOSE_2) {
        if (story_coordinator_is_action_allowed(action->type)) {
            perform_choice(action->type == STORY_ACTION_CHOOSE_1 ? 1 : 2);
        }
    } else {
        restart_from_title();
    }
}

static void audio_event_on_ui_thread(void *ctx)
{
    audio_session_event_ctx_t *event = (audio_session_event_ctx_t *)ctx;
    if (!event || event->session_id != s_active_audio_session_id) {
        return;
    }

    switch (event->event) {
    case AUDIO_SESSION_EVENT_BUFFERING:
        if (s_audio_sequence.clips && s_audio_index < s_audio_sequence.count) {
            story_presenter_set_audio_phase(s_audio_sequence.clips[s_audio_index].narrator
                                                ? STORY_PRESENTER_AUDIO_PHASE_BUFFERING
                                                : STORY_PRESENTER_AUDIO_PHASE_CUE);
        }
        break;
    case AUDIO_SESSION_EVENT_PLAYING:
        if (s_audio_sequence.clips && s_audio_index < s_audio_sequence.count) {
            story_presenter_set_audio_phase(s_audio_sequence.clips[s_audio_index].narrator
                                                ? STORY_PRESENTER_AUDIO_PHASE_PLAYING
                                                : STORY_PRESENTER_AUDIO_PHASE_CUE);
        }
        break;
    case AUDIO_SESSION_EVENT_FINISHED:
    case AUDIO_SESSION_EVENT_ERROR:
        if (event->event == AUDIO_SESSION_EVENT_ERROR) {
            ESP_LOGW(TAG, "audio clip failed: %s", esp_err_to_name(event->error));
        }
        s_active_audio_session_id = 0;
        ++s_audio_index;
        start_current_audio_clip();
        break;
    case AUDIO_SESSION_EVENT_PAUSED:
    case AUDIO_SESSION_EVENT_STOPPED:
        story_presenter_set_audio_phase(STORY_PRESENTER_AUDIO_PHASE_IDLE);
        break;
    }
}

static void free_audio_event(void *ctx)
{
    free(ctx);
}

void story_coordinator_handle_audio_event(const audio_session_event_ctx_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (!event || !ui_dispatch_is_ready()) {
        return;
    }

    audio_session_event_ctx_t *copy = malloc(sizeof(*copy));
    if (!copy) {
        return;
    }
    *copy = *event;
    if (ui_dispatch_post(audio_event_on_ui_thread, copy, free_audio_event, true, 0) != ESP_OK) {
        free(copy);
    }
}

story_app_mode_t story_coordinator_get_mode(void)
{
    return s_mode;
}

void story_coordinator_switch_mode(story_app_mode_t mode)
{
    if (mode != STORY_APP_MODE_LOOP && mode != STORY_APP_MODE_FULL) {
        return;
    }
    if (s_mode == mode) {
        return;
    }

    cancel_all_timers();
    stop_audio_smoothly();
    s_settings_open = false;
    s_mode = mode;
    if (mode == STORY_APP_MODE_LOOP) {
        s_next_loop_choice_is_play_again = true;
    }
    ui_request_restart_to_title();
}
