#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pcm_chunk_t
{
    uint8_t *data;
    size_t capacity;
    size_t size;
    uint32_t session_id;
    uint16_t index;
} pcm_chunk_t;

typedef struct pcm_chunk_pool_t pcm_chunk_pool_t;

esp_err_t pcm_chunk_pool_init(pcm_chunk_pool_t **out_pool, size_t chunk_count, size_t chunk_size);
void pcm_chunk_pool_destroy(pcm_chunk_pool_t *pool);

pcm_chunk_t *pcm_chunk_pool_acquire(pcm_chunk_pool_t *pool, TickType_t timeout_ticks);
esp_err_t pcm_chunk_pool_submit_ready(pcm_chunk_pool_t *pool, pcm_chunk_t *chunk, TickType_t timeout_ticks);
pcm_chunk_t *pcm_chunk_pool_receive_ready(pcm_chunk_pool_t *pool, TickType_t timeout_ticks);
esp_err_t pcm_chunk_pool_release(pcm_chunk_pool_t *pool, pcm_chunk_t *chunk);

size_t pcm_chunk_pool_ready_count(pcm_chunk_pool_t *pool);
void pcm_chunk_pool_reset(pcm_chunk_pool_t *pool);

#ifdef __cplusplus
}
#endif
