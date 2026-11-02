#include "story_engine.h"

#include <string.h>

#include "builtin_reading_texts.h"
#include "builtin_texts.h"

static void story_engine_apply_choice(story_engine_t *engine, bool first_choice)
{
    const story_node_t *node = NULL;

    if (!engine) {
        return;
    }

    node = story_get_node(engine->state.current_case);
    if (!node || node->is_final) {
        return;
    }

    engine->state.current_case = first_choice ? node->next1 : node->next2;
    builtin_text_set(engine->state.current_case);
    engine->state.phase = STORY_PHASE_SCENE;
}

void story_engine_init(story_engine_t *engine)
{
    if (!engine) {
        return;
    }

    memset(engine, 0, sizeof(*engine));
    engine->state.current_case = STORY_START_CASE;
    engine->state.phase = STORY_PHASE_INTRO;
    builtin_text_set(engine->state.current_case);
}

void story_engine_apply(story_engine_t *engine, const story_action_t *action)
{
    if (!engine || !action) {
        return;
    }

    switch (action->type) {
    case STORY_ACTION_START_STORY:
    case STORY_ACTION_RESTART_STORY:
        story_engine_init(engine);
        break;
    case STORY_ACTION_CHOOSE_1:
        story_engine_apply_choice(engine, true);
        break;
    case STORY_ACTION_CHOOSE_2:
        story_engine_apply_choice(engine, false);
        break;
    }
}

void story_engine_set_phase(story_engine_t *engine, story_phase_t phase)
{
    if (!engine) {
        return;
    }

    engine->state.phase = phase;
}

void story_engine_build_view_model(const story_engine_t *engine, story_view_model_t *out_model)
{
    if (!engine || !out_model) {
        return;
    }

    memset(out_model, 0, sizeof(*out_model));
    builtin_text_set(engine->state.current_case);

    out_model->state = engine->state;
    out_model->node = story_get_node(engine->state.current_case);
    out_model->reading_text = builtin_get_reading_text_for_case(engine->state.current_case);
    out_model->question_text = out_model->node ? out_model->node->question : "";
    out_model->choice1_text = out_model->node ? out_model->node->choice1 : "";
    out_model->choice2_text = out_model->node ? out_model->node->choice2 : "";
    out_model->is_final = out_model->node ? out_model->node->is_final : false;
    out_model->choice2_enabled = true;

    switch (engine->state.phase) {
    case STORY_PHASE_INTRO:
        out_model->speech_text = builtin_get_intro_text();
        break;
    case STORY_PHASE_OUTRO:
    case STORY_PHASE_COMPLETE:
        out_model->speech_text = builtin_get_outro_text();
        break;
    case STORY_PHASE_SCENE:
    case STORY_PHASE_CHOICE:
    default:
        out_model->speech_text = get_builtin_text();
        break;
    }
}
