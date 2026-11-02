#include "pcm_chunk_pool.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/queue.h"

struct pcm_chunk_pool_t
{
    pcm_chunk_t *chunks;
    uint8_t *storage;
    size_t chunk_count;
    size_t chunk_size;
    QueueHandle_t free_queue;
    QueueHandle_t ready_queue;
};

static const char *TAG = "pcm_chunk_pool";

esp_err_t pcm_chunk_pool_init(pcm_chunk_pool_t **out_pool, size_t chunk_count, size_t chunk_size)
{
    if (!out_pool || chunk_count == 0 || chunk_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    pcm_chunk_pool_t *pool = calloc(1, sizeof(*pool));
    if (!pool) {
        return ESP_ERR_NO_MEM;
    }

    pool->chunks = calloc(chunk_count, sizeof(*pool->chunks));
    pool->storage = heap_caps_malloc(chunk_count * chunk_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    pool->free_queue = xQueueCreate(chunk_count, sizeof(pcm_chunk_t *));
    pool->ready_queue = xQueueCreate(chunk_count, sizeof(pcm_chunk_t *));
    if (!pool->chunks || !pool->storage || !pool->free_queue || !pool->ready_queue) {
        pcm_chunk_pool_destroy(pool);
        return ESP_ERR_NO_MEM;
    }

    pool->chunk_count = chunk_count;
    pool->chunk_size = chunk_size;

    for (size_t i = 0; i < chunk_count; ++i) {
        pcm_chunk_t *chunk = &pool->chunks[i];
        chunk->data = pool->storage + (i * chunk_size);
        chunk->capacity = chunk_size;
        chunk->size = 0;
        chunk->session_id = 0;
        chunk->index = (uint16_t)i;
        if (xQueueSend(pool->free_queue, &chunk, 0) != pdTRUE) {
            pcm_chunk_pool_destroy(pool);
            return ESP_FAIL;
        }
    }

    *out_pool = pool;
    ESP_LOGI(TAG, "initialized %u chunks of %u bytes", (unsigned)chunk_count, (unsigned)chunk_size);
    return ESP_OK;
}

void pcm_chunk_pool_destroy(pcm_chunk_pool_t *pool)
{
    if (!pool) {
        return;
    }

    if (pool->free_queue) {
        vQueueDelete(pool->free_queue);
    }
    if (pool->ready_queue) {
        vQueueDelete(pool->ready_queue);
    }
    free(pool->chunks);
    if (pool->storage) {
        heap_caps_free(pool->storage);
    }
    free(pool);
}

pcm_chunk_t *pcm_chunk_pool_acquire(pcm_chunk_pool_t *pool, TickType_t timeout_ticks)
{
    if (!pool) {
        return NULL;
    }

    pcm_chunk_t *chunk = NULL;
    if (xQueueReceive(pool->free_queue, &chunk, timeout_ticks) == pdTRUE) {
        chunk->size = 0;
        chunk->session_id = 0;
        return chunk;
    }
    return NULL;
}

esp_err_t pcm_chunk_pool_submit_ready(pcm_chunk_pool_t *pool, pcm_chunk_t *chunk, TickType_t timeout_ticks)
{
    if (!pool || !chunk) {
        return ESP_ERR_INVALID_ARG;
    }
    return xQueueSend(pool->ready_queue, &chunk, timeout_ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

pcm_chunk_t *pcm_chunk_pool_receive_ready(pcm_chunk_pool_t *pool, TickType_t timeout_ticks)
{
    if (!pool) {
        return NULL;
    }

    pcm_chunk_t *chunk = NULL;
    if (xQueueReceive(pool->ready_queue, &chunk, timeout_ticks) == pdTRUE) {
        return chunk;
    }
    return NULL;
}

esp_err_t pcm_chunk_pool_release(pcm_chunk_pool_t *pool, pcm_chunk_t *chunk)
{
    if (!pool || !chunk) {
        return ESP_ERR_INVALID_ARG;
    }
    chunk->size = 0;
    chunk->session_id = 0;
    return xQueueSend(pool->free_queue, &chunk, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

size_t pcm_chunk_pool_ready_count(pcm_chunk_pool_t *pool)
{
    if (!pool) {
        return 0;
    }
    return uxQueueMessagesWaiting(pool->ready_queue);
}

void pcm_chunk_pool_reset(pcm_chunk_pool_t *pool)
{
    if (!pool) {
        return;
    }

    pcm_chunk_t *chunk = NULL;
    while (xQueueReceive(pool->ready_queue, &chunk, 0) == pdTRUE) {
        (void)pcm_chunk_pool_release(pool, chunk);
    }
}
