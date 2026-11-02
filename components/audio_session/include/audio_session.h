#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AUDIO_SESSION_EVENT_BUFFERING = 0,
    AUDIO_SESSION_EVENT_PLAYING,
    AUDIO_SESSION_EVENT_PAUSED,
    AUDIO_SESSION_EVENT_STOPPED,
    AUDIO_SESSION_EVENT_FINISHED,
    AUDIO_SESSION_EVENT_ERROR,
} audio_session_event_t;

typedef struct
{
    uint32_t session_id;
    audio_session_event_t event;
    esp_err_t error;
} audio_session_event_ctx_t;

typedef void (*audio_session_event_cb_t)(const audio_session_event_ctx_t *ctx, void *user_ctx);

typedef struct
{
    uint32_t active_session_id;
    uint32_t queue_depth;
    bool active;
    bool paused;
    esp_err_t last_error;
} audio_session_metrics_t;

esp_err_t audio_session_init(void);
void audio_session_deinit(void);
void audio_session_register_callback(audio_session_event_cb_t cb, void *user_ctx);

esp_err_t audio_session_preload_uri(const char *uri);
esp_err_t audio_session_start_uri(const char *uri, uint32_t *out_session_id);
esp_err_t audio_session_play_sfx_uri(const char *uri, int volume_percent);
esp_err_t audio_session_play_sfx_uri_with_fade_out(const char *uri,
                                                   int volume_percent,
                                                   uint32_t fade_out_delay_ms,
                                                   uint32_t fade_out_ms);
esp_err_t audio_session_fade_out_primary(uint32_t fade_out_ms);
esp_err_t audio_session_fade_out_sfx(uint32_t fade_out_ms);
esp_err_t audio_session_stop(void);
esp_err_t audio_session_pause(void);
esp_err_t audio_session_resume(void);
esp_err_t audio_session_set_volume(int volume, int *out_volume_set);
int audio_session_get_volume(void);
bool audio_session_is_muted(void);
void audio_session_get_metrics(audio_session_metrics_t *out_metrics);

#ifdef __cplusplus
}
#endif
