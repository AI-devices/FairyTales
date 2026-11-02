#include "story_presenter.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "audio_session.h"
#include "media_cache.h"
#include "media_loader_service.h"
#include "story_assets.h"
#include "ui_dispatch.h"
#include "ui_dual_gif_screen.h"
#include "ui_img_manager.h"

#define STORY_PRESENTER_SCENE_CACHE_CAPACITY 4U
#define STORY_PRESENTER_DISABLE_SCENE_PREFETCH 1
#define STORY_PRESENTER_SCENE_GUARD_SIZE 32U
#define STORY_PRESENTER_SCENE_GUARD_PATTERN 0xA5U

typedef struct
{
    builtin_text_case_t case_id;
    const char *uri;
    uint8_t *blob;
    size_t blob_size;
    lv_img_dsc_t image;
    uint32_t expected_crc32;
    bool guard_installed;
} story_presenter_scene_resource_t;

typedef struct
{
    uint32_t generation;
    story_view_model_t model;
} story_presenter_scene_load_req_t;

typedef struct
{
    uint32_t generation;
    builtin_text_case_t case_id;
} story_presenter_scene_prefetch_req_t;

static const char *TAG = "story_presenter";
static story_presenter_callbacks_t s_callbacks = {0};
static media_cache_slot_t s_scene_cache_slots[STORY_PRESENTER_SCENE_CACHE_CAPACITY] = {0};
static media_cache_t s_scene_cache = {0};
static volatile bool s_scene_prefetch_inflight[CASE_TXT_COUNT] = {0};
static volatile uint32_t s_scene_generation = 0;
static builtin_text_case_t s_active_case = CASE_TXT_COUNT;
static builtin_text_case_t s_previous_case = CASE_TXT_COUNT;
static story_presenter_scene_resource_t *s_pinned_scene_resource = NULL;
static story_presenter_scene_resource_t *s_previous_pinned_scene_resource = NULL;
static bool s_pinned_scene_committed = false;
static story_view_model_t s_pending_scene_model = {0};
static bool s_pending_scene_valid = false;
static bool s_pending_scene_commit = false;
static bool s_screen_scene_ready = false;

#if CONFIG_FAIRYTALE_VERBOSE_RUNTIME_LOGS
#define STORY_DIAG_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define STORY_DIAG_LOGI(...) do { } while (0)
#endif

static void story_presenter_apply_loaded_scene_on_ui_thread(void *ctx);
static void story_presenter_prefetch_next_scenes(const story_view_model_t *model);
static void story_presenter_request_scene_load(const story_view_model_t *model);
static void story_presenter_stage_scene(const story_view_model_t *model);
static bool story_presenter_try_commit_pending_scene(void);
static void story_presenter_commit_story_content(void);
static bool story_presenter_prepare_scene_resource_diagnostics(story_presenter_scene_resource_t *resource);
static void story_presenter_log_scene_resource_integrity(const char *stage,
                                                         const story_presenter_scene_resource_t *resource);
static void story_presenter_reset_runtime_state(bool reset_screen);
static bool story_presenter_scene_load_job_run(void *ctx, uint32_t generation);
static bool story_presenter_scene_load_job_stale(void *ctx, uint32_t generation);
static bool story_presenter_scene_prefetch_job_run(void *ctx, uint32_t generation);
static bool story_presenter_scene_prefetch_job_stale(void *ctx, uint32_t generation);
static void story_presenter_scene_prefetch_job_free(void *ctx);

static void story_presenter_scene_image_ready(void *user_ctx)
{
    (void)user_ctx;

    s_screen_scene_ready = true;
    if (story_presenter_try_commit_pending_scene()) {
        return;
    }

    if (s_pending_scene_commit) {
        return;
    }

    if (s_callbacks.scene_image_ready) {
        s_callbacks.scene_image_ready(s_callbacks.user_ctx);
    }
}

static void story_presenter_choice_selected(int choice_index, void *user_ctx)
{
    (void)user_ctx;

    if (s_callbacks.choice_selected) {
        s_callbacks.choice_selected(choice_index, s_callbacks.user_ctx);
    }
}

static void story_presenter_close_finished(void *user_ctx)
{
    (void)user_ctx;

    if (s_callbacks.close_finished) {
        s_callbacks.close_finished(s_callbacks.user_ctx);
    }
}

static void story_presenter_restart_requested(void *user_ctx)
{
    (void)user_ctx;

    if (s_callbacks.restart_requested) {
        s_callbacks.restart_requested(s_callbacks.user_ctx);
    }
}

static void story_presenter_settings_opened(void *user_ctx)
{
    (void)user_ctx;

    if (s_callbacks.settings_opened) {
        s_callbacks.settings_opened(s_callbacks.user_ctx);
    }
}

static void story_presenter_settings_closed(void *user_ctx)
{
    (void)user_ctx;

    if (s_callbacks.settings_closed) {
        s_callbacks.settings_closed(s_callbacks.user_ctx);
    }
}

static const char *story_presenter_scene_uri(builtin_text_case_t case_id)
{
    const story_asset_desc_t *asset = story_assets_get_desc(case_id);

    return asset ? asset->spiffs_path : NULL;
}

static const char *story_presenter_scene_node_name(builtin_text_case_t case_id)
{
    const story_asset_desc_t *asset = story_assets_get_desc(case_id);

    return asset ? asset->node_name : NULL;
}

static uint32_t story_presenter_scene_crc32(const story_presenter_scene_resource_t *resource)
{
    if (!resource || !resource->image.data || resource->image.data_size == 0U) {
        return 0U;
    }

    return esp_crc32_le(0U, resource->image.data, resource->image.data_size);
}

static bool story_presenter_scene_guard_ok(const story_presenter_scene_resource_t *resource, size_t *bad_offset)
{
    size_t i = 0U;
    const uint8_t *guard = NULL;

    if (bad_offset) {
        *bad_offset = 0U;
    }

    if (!resource || !resource->guard_installed || !resource->blob) {
        return true;
    }

    guard = resource->blob + resource->blob_size;
    for (i = 0U; i < STORY_PRESENTER_SCENE_GUARD_SIZE; ++i) {
        if (guard[i] != STORY_PRESENTER_SCENE_GUARD_PATTERN) {
            if (bad_offset) {
                *bad_offset = i;
            }
            return false;
        }
    }

    return true;
}

static void story_presenter_log_scene_resource_integrity(const char *stage,
                                                         const story_presenter_scene_resource_t *resource)
{
    const uint32_t actual_crc32 = story_presenter_scene_crc32(resource);
    size_t bad_offset = 0U;
    const bool guard_ok = story_presenter_scene_guard_ok(resource, &bad_offset);
    const bool crc_ok = resource && actual_crc32 == resource->expected_crc32;
    const char *level_tag = (guard_ok && crc_ok) ? "scene_integrity_ok" : "scene_integrity_bad";

    if (!resource) {
        ESP_LOGW(TAG, "%s stage=%s resource=none", level_tag, stage ? stage : "<null>");
        return;
    }

    if (guard_ok && crc_ok) {
        STORY_DIAG_LOGI("%s stage=%s case=%d node=%s uri=%s size=%u crc=%08" PRIx32,
                        level_tag,
                        stage ? stage : "<null>",
                        (int)resource->case_id,
                        story_presenter_scene_node_name(resource->case_id) ?
                        story_presenter_scene_node_name(resource->case_id) : "<null>",
                        resource->uri ? resource->uri : "<null>",
                        (unsigned)resource->image.data_size,
                        actual_crc32);
        return;
    }

    ESP_LOGE(TAG,
             "%s stage=%s case=%d node=%s uri=%s size=%u "
             "crc_expected=%08" PRIx32 " crc_actual=%08" PRIx32 " guard_ok=%u bad_guard_offset=%u",
             level_tag,
             stage ? stage : "<null>",
             (int)resource->case_id,
             story_presenter_scene_node_name(resource->case_id) ?
             story_presenter_scene_node_name(resource->case_id) : "<null>",
             resource->uri ? resource->uri : "<null>",
             (unsigned)resource->image.data_size,
             resource->expected_crc32,
             actual_crc32,
             guard_ok ? 1U : 0U,
             (unsigned)bad_offset);
}

static bool story_presenter_prepare_scene_resource_diagnostics(story_presenter_scene_resource_t *resource)
{
    uint8_t *blob = NULL;

    if (!resource || !resource->blob) {
        return false;
    }

    if (!resource->guard_installed) {
        blob = heap_caps_malloc(resource->blob_size + STORY_PRESENTER_SCENE_GUARD_SIZE,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!blob) {
            blob = heap_caps_malloc(resource->blob_size + STORY_PRESENTER_SCENE_GUARD_SIZE,
                                    MALLOC_CAP_8BIT);
        }
        if (!blob) {
            ESP_LOGW(TAG,
                     "scene_integrity_guard_alloc_failed case=%d node=%s uri=%s size=%u",
                     (int)resource->case_id,
                     story_presenter_scene_node_name(resource->case_id) ?
                     story_presenter_scene_node_name(resource->case_id) : "<null>",
                     resource->uri ? resource->uri : "<null>",
                     (unsigned)(resource->blob_size + STORY_PRESENTER_SCENE_GUARD_SIZE));
            return false;
        }

        memcpy(blob, resource->blob, resource->blob_size);
        memset(blob + resource->blob_size, STORY_PRESENTER_SCENE_GUARD_PATTERN, STORY_PRESENTER_SCENE_GUARD_SIZE);
        heap_caps_free(resource->blob);
        resource->blob = blob;
        resource->image.data = blob + sizeof(resource->image.header);
        resource->guard_installed = true;
    }

    resource->expected_crc32 = story_presenter_scene_crc32(resource);
    story_presenter_log_scene_resource_integrity("diag_prepare", resource);
    return true;
}

static void story_presenter_release_scene_resource(void *value)
{
    story_presenter_scene_resource_t *resource = (story_presenter_scene_resource_t *)value;

    if (!resource) {
        return;
    }

    story_presenter_log_scene_resource_integrity("release", resource);

    STORY_DIAG_LOGI("scene_resource release case=%d node=%s uri=%s size=%u",
                    (int)resource->case_id,
                    story_presenter_scene_node_name(resource->case_id) ?
                    story_presenter_scene_node_name(resource->case_id) : "<null>",
                    resource->uri ? resource->uri : "<null>",
                    (unsigned)resource->image.data_size);
    heap_caps_free(resource->blob);
    free(resource);
}

static void story_presenter_reset_pinned_scene_resources(void)
{
    if (s_previous_pinned_scene_resource) {
        story_presenter_release_scene_resource(s_previous_pinned_scene_resource);
        s_previous_pinned_scene_resource = NULL;
    }
    if (s_pinned_scene_resource) {
        story_presenter_release_scene_resource(s_pinned_scene_resource);
        s_pinned_scene_resource = NULL;
    }
    s_pinned_scene_committed = false;
}

static void story_presenter_reset_scene_cache(void)
{
    story_presenter_reset_pinned_scene_resources();
    media_cache_reset(&s_scene_cache);
    memset((void *)s_scene_prefetch_inflight, 0, sizeof(s_scene_prefetch_inflight));
    s_active_case = CASE_TXT_COUNT;
    s_previous_case = CASE_TXT_COUNT;
}

static story_presenter_scene_resource_t *story_presenter_get_scene_resource(builtin_text_case_t case_id)
{
    story_presenter_scene_resource_t *resource = NULL;
    const char *uri = story_presenter_scene_uri(case_id);

    if (!uri) {
        return NULL;
    }

    (void)media_cache_get(&s_scene_cache, uri, (void **)&resource, NULL);
    return resource;
}

static story_presenter_scene_resource_t *story_presenter_load_scene_resource(builtin_text_case_t case_id)
{
    story_presenter_scene_resource_t *resource = NULL;
    const story_asset_desc_t *asset = story_assets_get_desc(case_id);
    lv_img_header_t header = {0};
    uint8_t *blob = NULL;

    if (!asset) {
        return NULL;
    }

    blob = UI_LOAD_IMAGE(asset->spiffs_path, asset->raw_size);
    if (!blob) {
        ESP_LOGW(TAG, "failed to load story asset %s", asset->spiffs_path);
        return NULL;
    }

    if (asset->raw_size <= sizeof(header)) {
        ESP_LOGW(TAG, "story asset is too small: %s", asset->spiffs_path);
        heap_caps_free(blob);
        return NULL;
    }

    memcpy(&header, blob, sizeof(header));

    resource = calloc(1, sizeof(*resource));
    if (!resource) {
        heap_caps_free(blob);
        return NULL;
    }

    resource->case_id = case_id;
    resource->uri = asset->spiffs_path;
    resource->blob = blob;
    resource->blob_size = asset->raw_size;
    resource->image.header = header;
    resource->image.data = blob + sizeof(header);
    resource->image.data_size = asset->raw_size - sizeof(header);
    resource->expected_crc32 = story_presenter_scene_crc32(resource);
    STORY_DIAG_LOGI("scene_resource load case=%d node=%s uri=%s size=%u",
                    (int)case_id,
                    asset->node_name ? asset->node_name : "<null>",
                    asset->spiffs_path ? asset->spiffs_path : "<null>",
                    (unsigned)resource->image.data_size);
    return resource;
}

static bool story_presenter_ensure_scene_loaded(builtin_text_case_t case_id)
{
    story_presenter_scene_resource_t *resource = story_presenter_get_scene_resource(case_id);

    if (resource) {
        STORY_DIAG_LOGI("scene_resource hit case=%d node=%s uri=%s size=%u",
                        (int)case_id,
                        story_presenter_scene_node_name(case_id) ? story_presenter_scene_node_name(case_id) : "<null>",
                        resource->uri ? resource->uri : "<null>",
                        (unsigned)resource->image.data_size);
        return true;
    }

    resource = story_presenter_load_scene_resource(case_id);
    if (!resource) {
        return false;
    }

    if (!media_cache_put(&s_scene_cache, resource->uri, resource, resource->image.data_size)) {
        story_presenter_release_scene_resource(resource);
        return false;
    }

    STORY_DIAG_LOGI("scene_resource cache_put case=%d node=%s uri=%s size=%u",
                    (int)case_id,
                    story_presenter_scene_node_name(case_id) ? story_presenter_scene_node_name(case_id) : "<null>",
                    resource->uri ? resource->uri : "<null>",
                    (unsigned)resource->image.data_size);
    return true;
}

static const lv_img_dsc_t *story_presenter_get_scene_image(builtin_text_case_t case_id)
{
    story_presenter_scene_resource_t *resource = story_presenter_get_scene_resource(case_id);

    if (s_pinned_scene_resource && s_pinned_scene_resource->case_id == case_id) {
        return &s_pinned_scene_resource->image;
    }
    if (s_previous_pinned_scene_resource && s_previous_pinned_scene_resource->case_id == case_id) {
        return &s_previous_pinned_scene_resource->image;
    }
    return resource ? &resource->image : NULL;
}

static story_presenter_scene_resource_t *story_presenter_take_cached_scene_resource(builtin_text_case_t case_id)
{
    story_presenter_scene_resource_t *resource = NULL;
    const char *uri = story_presenter_scene_uri(case_id);

    if (!uri) {
        return NULL;
    }

    if (!media_cache_take(&s_scene_cache, uri, (void **)&resource, NULL) || !resource) {
        return NULL;
    }

    (void)story_presenter_prepare_scene_resource_diagnostics(resource);
    story_presenter_log_scene_resource_integrity("pin_take", resource);

    STORY_DIAG_LOGI("scene_resource pin_take case=%d node=%s uri=%s size=%u",
                    (int)resource->case_id,
                    story_presenter_scene_node_name(resource->case_id) ?
                    story_presenter_scene_node_name(resource->case_id) : "<null>",
                    resource->uri ? resource->uri : "<null>",
                    (unsigned)resource->image.data_size);
    return resource;
}

static story_presenter_scene_resource_t *story_presenter_pin_scene_resource(builtin_text_case_t case_id)
{
    story_presenter_scene_resource_t *pinned = NULL;

    if (case_id < 0 || case_id >= CASE_TXT_COUNT) {
        return NULL;
    }

    if (s_pinned_scene_resource && s_pinned_scene_resource->case_id == case_id) {
        return s_pinned_scene_resource;
    }

    pinned = story_presenter_take_cached_scene_resource(case_id);
    if (!pinned) {
        if (!story_presenter_ensure_scene_loaded(case_id)) {
            return NULL;
        }
        pinned = story_presenter_take_cached_scene_resource(case_id);
        if (!pinned) {
            return NULL;
        }
    }

    if (s_pinned_scene_resource) {
        if (s_pinned_scene_committed) {
            if (s_previous_pinned_scene_resource) {
                story_presenter_release_scene_resource(s_previous_pinned_scene_resource);
            }
            s_previous_pinned_scene_resource = s_pinned_scene_resource;
        } else {
            story_presenter_release_scene_resource(s_pinned_scene_resource);
        }
    }

    s_pinned_scene_resource = pinned;
    s_pinned_scene_committed = false;
    story_presenter_log_scene_resource_integrity("pin_active", pinned);

    STORY_DIAG_LOGI("scene_resource pin_active case=%d node=%s uri=%s size=%u previous=%u",
                    (int)pinned->case_id,
                    story_presenter_scene_node_name(pinned->case_id) ?
                    story_presenter_scene_node_name(pinned->case_id) : "<null>",
                    pinned->uri ? pinned->uri : "<null>",
                    (unsigned)pinned->image.data_size,
                    s_previous_pinned_scene_resource ? 1U : 0U);
    return pinned;
}

static void story_presenter_apply_scene_content(const story_view_model_t *model,
                                                const story_presenter_scene_resource_t *scene_resource)
{
    ui_dual_gif_story_content_t content = {0};
    const lv_img_dsc_t *overlay_image = scene_resource ? &scene_resource->image : NULL;

    if (!model) {
        return;
    }

    content.reading_text = model->reading_text;
    content.question_text = model->question_text;
    content.choice1_text = model->choice1_text;
    content.choice2_text = model->choice2_text;
    content.overlay_image = overlay_image;
    content.debug_case_id = (int)model->state.current_case;
    content.debug_node_name = story_presenter_scene_node_name(model->state.current_case);
    content.debug_image_uri = story_presenter_scene_uri(model->state.current_case);
    content.is_final_scene = model->is_final;
    content.choice2_enabled = model->choice2_enabled;
    story_presenter_log_scene_resource_integrity("scene_apply", scene_resource);
    ESP_LOGI(TAG,
             "scene_apply case=%d node=%s uri=%s size=%u final=%u",
             (int)model->state.current_case,
             content.debug_node_name ? content.debug_node_name : "<null>",
             content.debug_image_uri ? content.debug_image_uri : "<null>",
             overlay_image ? (unsigned)overlay_image->data_size : 0U,
             model->is_final ? 1U : 0U);
    ui_dual_gif_screen_set_story_content(&content);
}

static void story_presenter_stage_scene(const story_view_model_t *model)
{
    story_presenter_scene_resource_t *scene_resource = NULL;

    if (!model) {
        return;
    }

    scene_resource = story_presenter_pin_scene_resource(model->state.current_case);
    story_presenter_apply_scene_content(model, scene_resource);

    if (!scene_resource) {
        story_presenter_request_scene_load(model);
    }
}

static bool story_presenter_try_commit_pending_scene(void)
{
    story_presenter_scene_resource_t *scene_resource = NULL;

    if (!s_pending_scene_valid || !s_pending_scene_commit || !s_screen_scene_ready) {
        return false;
    }

    scene_resource = story_presenter_pin_scene_resource(s_pending_scene_model.state.current_case);
    if (!scene_resource) {
        return false;
    }

    story_presenter_apply_scene_content(&s_pending_scene_model, scene_resource);
    story_presenter_commit_story_content();
    s_pending_scene_commit = false;
    s_screen_scene_ready = false;

    if (s_callbacks.scene_image_ready) {
        s_callbacks.scene_image_ready(s_callbacks.user_ctx);
    }

    return true;
}

static void story_presenter_commit_story_content(void)
{
    ui_dual_gif_screen_commit_story_content();

    if (s_previous_pinned_scene_resource) {
        STORY_DIAG_LOGI("scene_resource pin_release_previous active=%u",
                        s_pinned_scene_resource ? 1U : 0U);
        story_presenter_release_scene_resource(s_previous_pinned_scene_resource);
        s_previous_pinned_scene_resource = NULL;
    }

    if (s_pinned_scene_resource) {
        s_pinned_scene_committed = true;
        story_presenter_log_scene_resource_integrity("pin_committed", s_pinned_scene_resource);
        STORY_DIAG_LOGI("scene_resource pin_committed case=%d node=%s uri=%s size=%u",
                        (int)s_pinned_scene_resource->case_id,
                        story_presenter_scene_node_name(s_pinned_scene_resource->case_id) ?
                        story_presenter_scene_node_name(s_pinned_scene_resource->case_id) : "<null>",
                        s_pinned_scene_resource->uri ? s_pinned_scene_resource->uri : "<null>",
                        (unsigned)s_pinned_scene_resource->image.data_size);
    }
}

static void story_presenter_request_scene_load(const story_view_model_t *model)
{
    story_presenter_scene_load_req_t *req = NULL;
    media_loader_request_t request = {0};

    if (!model) {
        return;
    }

    req = malloc(sizeof(*req));
    if (!req) {
        ESP_LOGW(TAG, "scene load request allocation failed");
        return;
    }

    memset(req, 0, sizeof(*req));
    req->generation = ++s_scene_generation;
    req->model = *model;

    request.debug_tag = "story_scene";
    request.ctx = req;
    request.generation = req->generation;
    request.priority = MEDIA_LOADER_PRIORITY_CRITICAL_NOW;
    request.run_cb = story_presenter_scene_load_job_run;
    request.stale_cb = story_presenter_scene_load_job_stale;
    request.free_cb = free;

    if (media_loader_service_enqueue(&request) != ESP_OK) {
        ESP_LOGW(TAG, "unable to enqueue story scene loader");
        free(req);
    }
}

static void story_presenter_request_scene_prefetch(builtin_text_case_t case_id)
{
    story_presenter_scene_prefetch_req_t *req = NULL;
    media_loader_request_t request = {0};

    if (case_id < 0 || case_id >= CASE_TXT_COUNT) {
        return;
    }

    if (story_presenter_get_scene_image(case_id) || s_scene_prefetch_inflight[case_id]) {
        return;
    }

    s_scene_prefetch_inflight[case_id] = true;

    req = malloc(sizeof(*req));
    if (!req) {
        s_scene_prefetch_inflight[case_id] = false;
        ESP_LOGW(TAG, "scene prefetch request allocation failed");
        return;
    }

    memset(req, 0, sizeof(*req));
    req->generation = s_scene_generation;
    req->case_id = case_id;

    request.debug_tag = "story_prefetch";
    request.ctx = req;
    request.generation = req->generation;
    request.priority = MEDIA_LOADER_PRIORITY_BRANCH_PREFETCH;
    request.run_cb = story_presenter_scene_prefetch_job_run;
    request.stale_cb = story_presenter_scene_prefetch_job_stale;
    request.free_cb = story_presenter_scene_prefetch_job_free;

    if (media_loader_service_enqueue(&request) != ESP_OK) {
        ESP_LOGW(TAG, "unable to enqueue story scene prefetch");
        story_presenter_scene_prefetch_job_free(req);
    }
}

static void story_presenter_prefetch_next_scenes(const story_view_model_t *model)
{
#if STORY_PRESENTER_DISABLE_SCENE_PREFETCH
    if (model) {
        STORY_DIAG_LOGI("scene_prefetch disabled case=%d node=%s",
                        (int)model->state.current_case,
                        story_presenter_scene_node_name(model->state.current_case) ?
                        story_presenter_scene_node_name(model->state.current_case) : "<null>");
    }
    return;
#endif

    builtin_text_case_t next1 = CASE_TXT_COUNT;
    builtin_text_case_t next2 = CASE_TXT_COUNT;

    if (!model || !model->node || model->node->is_final) {
        return;
    }

    next1 = model->node->next1;
    next2 = model->node->next2;

    if (next1 >= 0 && next1 < CASE_TXT_COUNT) {
        story_presenter_request_scene_prefetch(next1);
    }

    if (model->choice2_enabled &&
        next2 >= 0 && next2 < CASE_TXT_COUNT && next2 != next1) {
        story_presenter_request_scene_prefetch(next2);
    }
}

static bool story_presenter_scene_load_job_stale(void *ctx, uint32_t generation)
{
    (void)ctx;
    return generation != s_scene_generation;
}

static bool story_presenter_scene_load_job_run(void *ctx, uint32_t generation)
{
    story_presenter_scene_load_req_t *req = (story_presenter_scene_load_req_t *)ctx;

    if (!req || generation != req->generation) {
        return false;
    }

    (void)story_presenter_ensure_scene_loaded(req->model.state.current_case);

    if (!ui_dispatch_is_ready() ||
        ui_dispatch_post(story_presenter_apply_loaded_scene_on_ui_thread,
                         req,
                         free,
                         true,
                         0) != ESP_OK) {
        return false;
    }

    return true;
}

static bool story_presenter_scene_prefetch_job_stale(void *ctx, uint32_t generation)
{
    story_presenter_scene_prefetch_req_t *req = (story_presenter_scene_prefetch_req_t *)ctx;

    if (!req || req->case_id < 0 || req->case_id >= CASE_TXT_COUNT) {
        return true;
    }

    return generation != s_scene_generation || story_presenter_get_scene_image(req->case_id) != NULL;
}

static bool story_presenter_scene_prefetch_job_run(void *ctx, uint32_t generation)
{
    story_presenter_scene_prefetch_req_t *req = (story_presenter_scene_prefetch_req_t *)ctx;

    if (!req || generation != req->generation) {
        return false;
    }

    (void)story_presenter_ensure_scene_loaded(req->case_id);
    return false;
}

static void story_presenter_scene_prefetch_job_free(void *ctx)
{
    story_presenter_scene_prefetch_req_t *req = (story_presenter_scene_prefetch_req_t *)ctx;

    if (!req) {
        return;
    }

    if (req->case_id >= 0 && req->case_id < CASE_TXT_COUNT) {
        s_scene_prefetch_inflight[req->case_id] = false;
    }
    free(req);
}

static void story_presenter_apply_loaded_scene_on_ui_thread(void *ctx)
{
    story_presenter_scene_load_req_t *req = (story_presenter_scene_load_req_t *)ctx;
    story_presenter_scene_resource_t *scene_resource = NULL;

    if (!req) {
        return;
    }

    if (req->generation != s_scene_generation) {
        return;
    }

    scene_resource = story_presenter_pin_scene_resource(req->model.state.current_case);
    if (!scene_resource) {
        ESP_LOGW(TAG, "scene image still unavailable for case=%d", (int)req->model.state.current_case);
        return;
    }

    story_presenter_apply_scene_content(&req->model, scene_resource);
    if (s_pending_scene_valid &&
        s_pending_scene_model.state.current_case == req->model.state.current_case &&
        s_pending_scene_commit) {
        (void)story_presenter_try_commit_pending_scene();
        return;
    }

    if (!s_pending_scene_commit && s_active_case == req->model.state.current_case) {
        story_presenter_commit_story_content();
    }
}

void story_presenter_init(const story_presenter_callbacks_t *callbacks)
{
    ui_dual_gif_screen_callbacks_t screen_callbacks = {
        .scene_image_ready = story_presenter_scene_image_ready,
        .choice_selected = story_presenter_choice_selected,
        .close_finished = story_presenter_close_finished,
        .restart_requested = story_presenter_restart_requested,
        .settings_opened = story_presenter_settings_opened,
        .settings_closed = story_presenter_settings_closed,
        .user_ctx = NULL,
    };

    memset(&s_callbacks, 0, sizeof(s_callbacks));
    if (callbacks) {
        s_callbacks = *callbacks;
    }

    if (!s_scene_cache.lock) {
        (void)media_cache_init(&s_scene_cache,
                               s_scene_cache_slots,
                               STORY_PRESENTER_SCENE_CACHE_CAPACITY,
                               story_presenter_release_scene_resource);
    }

    s_scene_generation = 0;
    story_presenter_reset_scene_cache();
    memset(&s_pending_scene_model, 0, sizeof(s_pending_scene_model));
    s_pending_scene_valid = false;
    s_pending_scene_commit = false;
    s_screen_scene_ready = false;
    ui_dual_gif_screen_register_callbacks(&screen_callbacks);
}

static void story_presenter_reset_runtime_state(bool reset_screen)
{
    ++s_scene_generation;
    memset((void *)s_scene_prefetch_inflight, 0, sizeof(s_scene_prefetch_inflight));
    s_active_case = CASE_TXT_COUNT;
    s_previous_case = CASE_TXT_COUNT;
    memset(&s_pending_scene_model, 0, sizeof(s_pending_scene_model));
    s_pending_scene_valid = false;
    s_pending_scene_commit = false;
    s_screen_scene_ready = false;

    if (reset_screen) {
        ui_dual_gif_screen_reset_story_state();
    }
}

void story_presenter_reset_state(void)
{
    story_presenter_reset_runtime_state(false);
}

void story_presenter_reset_story(void)
{
    story_presenter_reset_runtime_state(true);
}

void story_presenter_start_open(const story_view_model_t *model)
{
    if (model) {
        s_previous_case = s_active_case;
        s_active_case = model->state.current_case;
        s_pending_scene_model = *model;
        s_pending_scene_valid = true;
        s_pending_scene_commit = true;
        s_screen_scene_ready = false;
    }
    story_presenter_stage_scene(model);
    ui_dual_gif_screen_start_open();
}

void story_presenter_start_swipe(const story_view_model_t *model)
{
    if (model) {
        s_previous_case = s_active_case;
        s_active_case = model->state.current_case;
        s_pending_scene_model = *model;
        s_pending_scene_valid = true;
        s_pending_scene_commit = true;
        s_screen_scene_ready = false;
    }
    story_presenter_stage_scene(model);
    ui_dual_gif_screen_start_swipe();
}

void story_presenter_start_close(void)
{
    ui_dual_gif_screen_start_close();
}

void story_presenter_show_choices(const story_view_model_t *model)
{
    if (model) {
        s_previous_case = s_active_case;
        s_active_case = model->state.current_case;
        s_pending_scene_model = *model;
        s_pending_scene_valid = true;
    }
    s_pending_scene_commit = false;
    s_screen_scene_ready = false;
    story_presenter_stage_scene(model);
    story_presenter_commit_story_content();
    story_presenter_prefetch_next_scenes(model);
    ui_dual_gif_screen_show_choices();
}

void story_presenter_set_choice_pressed(int choice_index, bool pressed)
{
    ui_dual_gif_screen_set_choice_pressed(choice_index, pressed);
}

void story_presenter_click_choice(int choice_index)
{
    ui_dual_gif_screen_click_choice(choice_index);
}

void story_presenter_preload_close(void)
{
    ui_dual_gif_screen_preload_close();
}

void story_presenter_show_cloud(void)
{
    ui_dual_gif_screen_show_cloud();
}

void story_presenter_set_restart_pressed(bool pressed)
{
    ui_dual_gif_screen_set_restart_pressed(pressed);
}

void story_presenter_click_restart(void)
{
    ui_dual_gif_screen_click_restart();
}

void story_presenter_set_audio_phase(story_presenter_audio_phase_t phase)
{
    bool muted = audio_session_is_muted();

    switch (phase) {
    case STORY_PRESENTER_AUDIO_PHASE_BUFFERING:
        ui_dual_gif_screen_set_audio_active(true);
        ui_dual_gif_screen_set_bird_state(muted ? UI_DUAL_GIF_BIRD_STATE_IDLE :
                                                  UI_DUAL_GIF_BIRD_STATE_PRE_TALK);
        break;

    case STORY_PRESENTER_AUDIO_PHASE_PLAYING:
        ui_dual_gif_screen_set_audio_active(true);
        ui_dual_gif_screen_set_bird_state(muted ? UI_DUAL_GIF_BIRD_STATE_IDLE :
                                                  UI_DUAL_GIF_BIRD_STATE_TALK_LOOP);
        break;

    case STORY_PRESENTER_AUDIO_PHASE_CUE:
        ui_dual_gif_screen_set_audio_active(true);
        ui_dual_gif_screen_set_bird_state(UI_DUAL_GIF_BIRD_STATE_IDLE);
        break;

    case STORY_PRESENTER_AUDIO_PHASE_IDLE:
    default:
        ui_dual_gif_screen_set_audio_active(false);
        ui_dual_gif_screen_set_bird_state(UI_DUAL_GIF_BIRD_STATE_IDLE);
        break;
    }
}

void story_presenter_set_talking(bool talking)
{
    story_presenter_set_audio_phase(talking ? STORY_PRESENTER_AUDIO_PHASE_PLAYING :
                                              STORY_PRESENTER_AUDIO_PHASE_IDLE);
}
