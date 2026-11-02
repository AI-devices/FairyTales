#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MEDIA_LOADER_PRIORITY_CRITICAL_NOW = 0,
    MEDIA_LOADER_PRIORITY_NEXT_TRANSITION,
    MEDIA_LOADER_PRIORITY_BRANCH_PREFETCH,
    MEDIA_LOADER_PRIORITY_IDLE_PREPARE,
} media_loader_priority_t;

typedef bool (*media_loader_run_cb_t)(void *ctx, uint32_t generation);
typedef bool (*media_loader_stale_cb_t)(void *ctx, uint32_t generation);
typedef void (*media_loader_free_cb_t)(void *ctx);

typedef struct
{
    const char *debug_tag;
    void *ctx;
    uint32_t generation;
    media_loader_priority_t priority;
    media_loader_run_cb_t run_cb;
    media_loader_stale_cb_t stale_cb;
    media_loader_free_cb_t free_cb;
} media_loader_request_t;

esp_err_t media_loader_service_init(void);
bool media_loader_service_is_ready(void);
esp_err_t media_loader_service_enqueue(const media_loader_request_t *request);

#ifdef __cplusplus
}
#endif
