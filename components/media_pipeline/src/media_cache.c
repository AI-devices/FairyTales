#include "media_cache.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static bool media_cache_keys_match(const char *lhs, const char *rhs)
{
    if (lhs == rhs) {
        return true;
    }
    if (!lhs || !rhs) {
        return false;
    }
    return strcmp(lhs, rhs) == 0;
}

static void media_cache_release_slot(const media_cache_t *cache, media_cache_slot_t *slot)
{
    if (!slot || !slot->valid) {
        return;
    }

    if (cache && cache->release_fn && slot->value) {
        cache->release_fn(slot->value);
    }

    memset(slot, 0, sizeof(*slot));
}

static media_cache_slot_t *media_cache_find_slot(media_cache_t *cache, const char *key)
{
    size_t i = 0;

    if (!cache || !key) {
        return NULL;
    }

    for (i = 0; i < cache->capacity; ++i) {
        if (cache->slots[i].valid && media_cache_keys_match(cache->slots[i].key, key)) {
            return &cache->slots[i];
        }
    }

    return NULL;
}

static media_cache_slot_t *media_cache_pick_slot(media_cache_t *cache)
{
    media_cache_slot_t *candidate = NULL;
    size_t i = 0;

    if (!cache) {
        return NULL;
    }

    for (i = 0; i < cache->capacity; ++i) {
        media_cache_slot_t *slot = &cache->slots[i];

        if (!slot->valid) {
            return slot;
        }
        if (!candidate || slot->stamp < candidate->stamp) {
            candidate = slot;
        }
    }

    return candidate;
}

esp_err_t media_cache_init(media_cache_t *cache,
                           media_cache_slot_t *slots,
                           size_t capacity,
                           media_cache_release_fn_t release_fn)
{
    if (!cache || !slots || capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(cache, 0, sizeof(*cache));
    memset(slots, 0, capacity * sizeof(*slots));
    cache->slots = slots;
    cache->capacity = capacity;
    cache->release_fn = release_fn;
    cache->lock = xSemaphoreCreateMutex();
    if (!cache->lock) {
        memset(cache, 0, sizeof(*cache));
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void media_cache_deinit(media_cache_t *cache)
{
    SemaphoreHandle_t lock = NULL;

    if (!cache) {
        return;
    }

    media_cache_reset(cache);
    lock = (SemaphoreHandle_t)cache->lock;
    if (lock) {
        vSemaphoreDelete(lock);
    }
    memset(cache, 0, sizeof(*cache));
}

void media_cache_reset(media_cache_t *cache)
{
    SemaphoreHandle_t lock = NULL;
    size_t i = 0;

    if (!cache || !cache->slots) {
        return;
    }

    lock = (SemaphoreHandle_t)cache->lock;
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    for (i = 0; i < cache->capacity; ++i) {
        media_cache_release_slot(cache, &cache->slots[i]);
    }
    cache->stamp = 0U;

    xSemaphoreGive(lock);
}

bool media_cache_get(media_cache_t *cache, const char *key, void **out_value, size_t *out_size)
{
    media_cache_slot_t *slot = NULL;
    SemaphoreHandle_t lock = NULL;
    bool found = false;

    if (!cache || !key) {
        return false;
    }

    lock = (SemaphoreHandle_t)cache->lock;
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    slot = media_cache_find_slot(cache, key);
    if (slot) {
        slot->stamp = ++cache->stamp;
        if (out_value) {
            *out_value = slot->value;
        }
        if (out_size) {
            *out_size = slot->size;
        }
        found = true;
    }

    xSemaphoreGive(lock);
    return found;
}

bool media_cache_peek(media_cache_t *cache, const char *key, void **out_value, size_t *out_size)
{
    media_cache_slot_t *slot = NULL;
    SemaphoreHandle_t lock = NULL;
    bool found = false;

    if (!cache || !key) {
        return false;
    }

    lock = (SemaphoreHandle_t)cache->lock;
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    slot = media_cache_find_slot(cache, key);
    if (slot) {
        if (out_value) {
            *out_value = slot->value;
        }
        if (out_size) {
            *out_size = slot->size;
        }
        found = true;
    }

    xSemaphoreGive(lock);
    return found;
}

bool media_cache_take(media_cache_t *cache, const char *key, void **out_value, size_t *out_size)
{
    media_cache_slot_t *slot = NULL;
    SemaphoreHandle_t lock = NULL;
    bool found = false;

    if (!cache || !key) {
        return false;
    }

    lock = (SemaphoreHandle_t)cache->lock;
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    slot = media_cache_find_slot(cache, key);
    if (slot) {
        if (out_value) {
            *out_value = slot->value;
        }
        if (out_size) {
            *out_size = slot->size;
        }
        slot->value = NULL;
        memset(slot, 0, sizeof(*slot));
        found = true;
    }

    xSemaphoreGive(lock);
    return found;
}

bool media_cache_put(media_cache_t *cache, const char *key, void *value, size_t size)
{
    media_cache_slot_t *slot = NULL;
    SemaphoreHandle_t lock = NULL;

    if (!cache || !cache->slots || !key || !value) {
        return false;
    }

    lock = (SemaphoreHandle_t)cache->lock;
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    slot = media_cache_find_slot(cache, key);
    if (!slot) {
        slot = media_cache_pick_slot(cache);
    }
    if (!slot) {
        xSemaphoreGive(lock);
        return false;
    }

    media_cache_release_slot(cache, slot);
    slot->key = key;
    slot->value = value;
    slot->size = size;
    slot->stamp = ++cache->stamp;
    slot->valid = true;

    xSemaphoreGive(lock);
    return true;
}
