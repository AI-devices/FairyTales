#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*media_cache_release_fn_t)(void *value);

typedef struct
{
    const char *key;
    void *value;
    size_t size;
    uint32_t stamp;
    bool valid;
} media_cache_slot_t;

typedef struct
{
    media_cache_slot_t *slots;
    size_t capacity;
    uint32_t stamp;
    void *lock;
    media_cache_release_fn_t release_fn;
} media_cache_t;

esp_err_t media_cache_init(media_cache_t *cache,
                           media_cache_slot_t *slots,
                           size_t capacity,
                           media_cache_release_fn_t release_fn);
void media_cache_deinit(media_cache_t *cache);
void media_cache_reset(media_cache_t *cache);
bool media_cache_get(media_cache_t *cache, const char *key, void **out_value, size_t *out_size);
bool media_cache_peek(media_cache_t *cache, const char *key, void **out_value, size_t *out_size);
bool media_cache_take(media_cache_t *cache, const char *key, void **out_value, size_t *out_size);
bool media_cache_put(media_cache_t *cache, const char *key, void *value, size_t size);

#ifdef __cplusplus
}
#endif
