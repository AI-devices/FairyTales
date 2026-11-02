#include "fairytale_runtime.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"

#include "bsp/display.h"
#include "audio_session.h"
#include "media_loader_service.h"
#include "settings_store.h"
#include "story_audio.h"
#include "story_coordinator.h"
#include "ui_dispatch.h"

static const char *TAG = "fairytale_runtime";
static const int kDefaultVolume = 80;
static const int kDefaultBrightness = 100;
static const int kMinBrightness = 10;
static bool s_initialized = false;

typedef struct
{
    fairytale_runtime_progress_cb_t callback;
    void *user_ctx;
} runtime_preload_progress_ctx_t;

static void report_progress(fairytale_runtime_progress_cb_t callback,
                            void *user_ctx,
                            uint8_t percent,
                            const char *stage)
{
    if (callback) {
        callback(percent, stage, user_ctx);
    }
}

static void narration_preload_progress(size_t completed, size_t total, void *user_ctx)
{
    runtime_preload_progress_ctx_t *ctx = (runtime_preload_progress_ctx_t *)user_ctx;
    if (!ctx || total == 0U) {
        return;
    }

    const uint8_t percent = (uint8_t)(12U + ((completed * 33U) / total));
    report_progress(ctx->callback, ctx->user_ctx, percent, "Preparing narration");
}

static esp_err_t apply_persisted_settings(void)
{
    int32_t volume = kDefaultVolume;
    int32_t brightness = kDefaultBrightness;
    int applied_volume = kDefaultVolume;
    bool found = false;

    ESP_RETURN_ON_ERROR(settings_store_get_volume(&volume, &found),
                        TAG,
                        "read saved volume failed");
    if (!found) {
        volume = kDefaultVolume;
    }
    ESP_RETURN_ON_ERROR(audio_session_set_volume((int)volume, &applied_volume),
                        TAG,
                        "apply volume failed");

    found = false;
    ESP_RETURN_ON_ERROR(settings_store_get_display_brightness(&brightness, &found),
                        TAG,
                        "read saved brightness failed");
    if (!found) {
        brightness = kDefaultBrightness;
    }
    if (brightness < kMinBrightness) {
        brightness = kMinBrightness;
    } else if (brightness > 100) {
        brightness = 100;
    }

    return bsp_display_brightness_set((int)brightness);
}

esp_err_t fairytale_runtime_init(fairytale_runtime_progress_cb_t progress_cb,
                                 void *user_ctx)
{
    runtime_preload_progress_ctx_t preload_progress = {
        .callback = progress_cb,
        .user_ctx = user_ctx,
    };

    if (s_initialized) {
        report_progress(progress_cb, user_ctx, 45U, "Audio ready");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(settings_store_init(), TAG, "settings store init failed");
    report_progress(progress_cb, user_ctx, 6U, "Initializing settings");
    ESP_RETURN_ON_ERROR(ui_dispatch_init(), TAG, "UI dispatch init failed");
    report_progress(progress_cb, user_ctx, 8U, "Initializing interface");
    ESP_RETURN_ON_ERROR(media_loader_service_init(), TAG, "media loader init failed");
    report_progress(progress_cb, user_ctx, 10U, "Initializing media");
    ESP_RETURN_ON_ERROR(audio_session_init(), TAG, "audio session init failed");
    ESP_RETURN_ON_ERROR(apply_persisted_settings(), TAG, "apply persisted settings failed");
    report_progress(progress_cb, user_ctx, 12U, "Preparing narration");

    const esp_err_t narration_preload_err = story_audio_preload_narration(narration_preload_progress,
                                                                          &preload_progress);
    if (narration_preload_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "one or more narration files were not preloaded: %s",
                 esp_err_to_name(narration_preload_err));
    }

    audio_session_register_callback(story_coordinator_handle_audio_event, NULL);
    s_initialized = true;
    report_progress(progress_cb, user_ctx, 45U, "Audio ready");
    ESP_LOGI(TAG, "runtime initialized with local audio");
    return ESP_OK;
}
