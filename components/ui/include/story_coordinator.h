#pragma once

#include <stdbool.h>

#include "audio_session.h"
#include "story_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    STORY_APP_MODE_LOOP = 0,
    STORY_APP_MODE_FULL,
} story_app_mode_t;

void story_coordinator_init(void);
void story_coordinator_start(void);
void story_coordinator_reset(void);
void story_coordinator_handle_action(const story_action_t *action);
bool story_coordinator_is_action_allowed(story_action_type_t action_type);
void story_coordinator_handle_audio_event(const audio_session_event_ctx_t *event, void *user_ctx);
story_app_mode_t story_coordinator_get_mode(void);
void story_coordinator_switch_mode(story_app_mode_t mode);

#ifdef __cplusplus
}
#endif
