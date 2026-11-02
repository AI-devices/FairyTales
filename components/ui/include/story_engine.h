#pragma once

#include <stdbool.h>

#include "builtin_texts.h"
#include "story_fairy_tale.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    STORY_PHASE_INTRO = 0,
    STORY_PHASE_SCENE,
    STORY_PHASE_CHOICE,
    STORY_PHASE_OUTRO,
    STORY_PHASE_COMPLETE,
} story_phase_t;

typedef enum
{
    STORY_ACTION_START_STORY = 0,
    STORY_ACTION_RESTART_STORY,
    STORY_ACTION_CHOOSE_1,
    STORY_ACTION_CHOOSE_2,
} story_action_type_t;

typedef struct
{
    story_action_type_t type;
} story_action_t;

typedef struct
{
    builtin_text_case_t current_case;
    story_phase_t phase;
} story_state_t;

typedef struct
{
    story_state_t state;
} story_engine_t;

typedef struct
{
    story_state_t state;
    const story_node_t *node;
    const char *reading_text;
    const char *speech_text;
    const char *question_text;
    const char *choice1_text;
    const char *choice2_text;
    bool is_final;
    bool choice2_enabled;
} story_view_model_t;

void story_engine_init(story_engine_t *engine);
void story_engine_apply(story_engine_t *engine, const story_action_t *action);
void story_engine_set_phase(story_engine_t *engine, story_phase_t phase);
void story_engine_build_view_model(const story_engine_t *engine, story_view_model_t *out_model);

#ifdef __cplusplus
}
#endif
