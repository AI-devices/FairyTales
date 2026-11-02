#include "animation_cfa_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "lz4_block.h"
#include "media_storage.h"

#ifndef CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES
#define CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES 20971520
#endif

#ifndef CONFIG_FAIRYTALE_ANIM_CFA_FREE_PSRAM_MARGIN_BYTES
#define CONFIG_FAIRYTALE_ANIM_CFA_FREE_PSRAM_MARGIN_BYTES 6291456
#endif

#define CFA_MAGIC_0 'C'
#define CFA_MAGIC_1 'F'
#define CFA_MAGIC_2 'A'
#define CFA_MAGIC_3 '1'
#define CFA_VERSION 1U
#define CFA_HEADER_SIZE 44U
#define CFA_FRAME_SIZE 32U

/*
 * SD reads of a complete CFA can take several seconds on the panel.  A single
 * multi-megabyte fread keeps the loader task runnable for that whole period on
 * some SD/FATFS combinations, which can starve the idle task long enough to
 * trip the task watchdog.  Keep the mandatory in-PSRAM preload, but perform it
 * cooperatively in bounded chunks.
 */
#define CFA_PRELOAD_READ_CHUNK_SIZE (32U * 1024U)
#define CFA_PRELOAD_READ_YIELD_TICKS 1U

#define CFA_CODEC_RAW       0U
#define CFA_CODEC_RLE24     1U
#define CFA_CODEC_LZ4_BLOCK 2U

typedef struct __attribute__((packed))
{
    uint8_t magic[4];
    uint16_t version;
    uint16_t header_size;
    uint16_t canvas_width;
    uint16_t canvas_height;
    uint32_t frame_count;
    uint32_t flags;
    uint32_t index_offset;
    uint32_t payload_offset;
    uint32_t max_payload_size;
    uint32_t max_rect_pixels;
    uint32_t file_size;
    uint32_t header_crc32;
} cfa_file_header_t;

typedef struct __attribute__((packed))
{
    uint16_t delay_ms;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t flags;
    uint8_t codec;
    uint8_t reserved0;
    uint32_t raw_size;
    uint32_t payload_size;
    uint32_t payload_offset;
    uint32_t payload_crc32;
    uint16_t reserved1;
} cfa_frame_record_t;

struct animation_cfa_decoder
{
    char *uri;
    cfa_file_header_t header;
    cfa_frame_record_t *frames;
    uint8_t *file_data;
    size_t file_data_size;
    uint8_t *rect_buf;
    size_t rect_buf_capacity;
    size_t next_frame;
    size_t current_frame;
    animation_cfa_frame_profile_t last_profile;
    bool frame_ready;
};

static const char *TAG = "anim_cfa";

static uint16_t cfa_read_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t cfa_read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static uint32_t cfa_crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    size_t i = 0;

    crc = ~crc;
    for (i = 0; i < size; ++i) {
        uint32_t byte = data[i];
        int bit = 0;

        crc ^= byte;
        for (bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }

    return ~crc;
}

static uint32_t cfa_crc32(const uint8_t *data, size_t size)
{
    return cfa_crc32_update(0U, data, size);
}

static bool cfa_header_magic_valid(const cfa_file_header_t *header)
{
    return header &&
           header->magic[0] == (uint8_t)CFA_MAGIC_0 &&
           header->magic[1] == (uint8_t)CFA_MAGIC_1 &&
           header->magic[2] == (uint8_t)CFA_MAGIC_2 &&
           header->magic[3] == (uint8_t)CFA_MAGIC_3;
}

static void *cfa_malloc(size_t size)
{
    void *ptr = NULL;

    if (size == 0U) {
        return NULL;
    }

    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static esp_err_t cfa_read_exact(FILE *fp, void *dst, size_t size)
{
    if (!fp || (!dst && size != 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (size == 0U) {
        return ESP_OK;
    }
    return fread(dst, 1, size, fp) == size ? ESP_OK : ESP_FAIL;
}

static esp_err_t cfa_read_exact_cooperative(FILE *fp, void *dst, size_t size)
{
    uint8_t *write_ptr = (uint8_t *)dst;
    size_t remaining = size;

    if (!fp || (!dst && size != 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    while (remaining > 0U) {
        const size_t chunk_size = remaining > CFA_PRELOAD_READ_CHUNK_SIZE ?
                                      CFA_PRELOAD_READ_CHUNK_SIZE : remaining;

        if (fread(write_ptr, 1, chunk_size, fp) != chunk_size) {
            return ESP_FAIL;
        }

        write_ptr += chunk_size;
        remaining -= chunk_size;
        if (remaining > 0U) {
            vTaskDelay(CFA_PRELOAD_READ_YIELD_TICKS);
        }
    }

    return ESP_OK;
}

static esp_err_t cfa_validate_header_bytes(const uint8_t *header_bytes, cfa_file_header_t *out_header)
{
    cfa_file_header_t header = {0};
    uint32_t expected_crc = 0;
    uint32_t canvas_pixels = 0;

    if (!header_bytes || !out_header) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(header.magic, header_bytes, sizeof(header.magic));
    header.version = cfa_read_le16(header_bytes + 4);
    header.header_size = cfa_read_le16(header_bytes + 6);
    header.canvas_width = cfa_read_le16(header_bytes + 8);
    header.canvas_height = cfa_read_le16(header_bytes + 10);
    header.frame_count = cfa_read_le32(header_bytes + 12);
    header.flags = cfa_read_le32(header_bytes + 16);
    header.index_offset = cfa_read_le32(header_bytes + 20);
    header.payload_offset = cfa_read_le32(header_bytes + 24);
    header.max_payload_size = cfa_read_le32(header_bytes + 28);
    header.max_rect_pixels = cfa_read_le32(header_bytes + 32);
    header.file_size = cfa_read_le32(header_bytes + 36);
    header.header_crc32 = cfa_read_le32(header_bytes + 40);

    if (!cfa_header_magic_valid(&header) ||
        header.version != CFA_VERSION ||
        header.header_size != CFA_HEADER_SIZE ||
        header.index_offset != CFA_HEADER_SIZE ||
        header.frame_count == 0U ||
        header.canvas_width == 0U ||
        header.canvas_height == 0U) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    expected_crc = cfa_crc32(header_bytes, CFA_HEADER_SIZE - sizeof(uint32_t));
    if (expected_crc != header.header_crc32) {
        return ESP_ERR_INVALID_CRC;
    }

    canvas_pixels = (uint32_t)header.canvas_width * (uint32_t)header.canvas_height;
    if (header.max_rect_pixels > canvas_pixels ||
        header.max_payload_size > header.file_size ||
        header.payload_offset > header.file_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    *out_header = header;
    return ESP_OK;
}

static esp_err_t cfa_read_frame_record(const uint8_t *src, cfa_frame_record_t *out_record)
{
    cfa_frame_record_t record = {0};

    if (!src || !out_record) {
        return ESP_ERR_INVALID_ARG;
    }

    record.delay_ms = cfa_read_le16(src);
    record.x = cfa_read_le16(src + 2);
    record.y = cfa_read_le16(src + 4);
    record.w = cfa_read_le16(src + 6);
    record.h = cfa_read_le16(src + 8);
    record.flags = cfa_read_le16(src + 10);
    record.codec = src[12];
    record.reserved0 = src[13];
    record.raw_size = cfa_read_le32(src + 14);
    record.payload_size = cfa_read_le32(src + 18);
    record.payload_offset = cfa_read_le32(src + 22);
    record.payload_crc32 = cfa_read_le32(src + 26);
    record.reserved1 = cfa_read_le16(src + 30);

    *out_record = record;
    return ESP_OK;
}

static esp_err_t cfa_validate_frame_record(const cfa_file_header_t *header,
                                           const cfa_frame_record_t *record,
                                           size_t file_size)
{
    uint32_t rect_pixels = 0;

    if (!header || !record) {
        return ESP_ERR_INVALID_ARG;
    }

    if (record->codec != CFA_CODEC_RAW &&
        record->codec != CFA_CODEC_RLE24 &&
        record->codec != CFA_CODEC_LZ4_BLOCK) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if ((record->flags & ANIMATION_CFA_FRAME_EMPTY) != 0U) {
        if (record->w != 0U || record->h != 0U || record->raw_size != 0U || record->payload_size != 0U) {
            return ESP_ERR_INVALID_SIZE;
        }
        return ESP_OK;
    }

    if (record->w == 0U || record->h == 0U ||
        record->x >= header->canvas_width ||
        record->y >= header->canvas_height ||
        (uint32_t)record->x + record->w > header->canvas_width ||
        (uint32_t)record->y + record->h > header->canvas_height) {
        return ESP_ERR_INVALID_SIZE;
    }

    rect_pixels = (uint32_t)record->w * (uint32_t)record->h;
    if (record->raw_size != rect_pixels * 3U ||
        record->raw_size > header->max_rect_pixels * 3U ||
        record->payload_size > header->max_payload_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    if ((uint64_t)record->payload_offset + record->payload_size > file_size ||
        record->payload_offset < header->payload_offset) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t cfa_decode_rle24(const uint8_t *src,
                                  size_t src_size,
                                  uint8_t *dst,
                                  size_t dst_size,
                                  size_t pixel_count)
{
    const size_t rgb_size = pixel_count * 2U;
    size_t src_off = 0;
    size_t px = 0;

    if (!src || !dst || dst_size != pixel_count * 3U) {
        return ESP_ERR_INVALID_ARG;
    }

    while (src_off < src_size) {
        uint16_t run = 0;
        uint8_t rgb0 = 0;
        uint8_t rgb1 = 0;
        uint8_t alpha = 0;
        uint16_t i = 0;

        if (src_off + 5U > src_size) {
            return ESP_ERR_INVALID_SIZE;
        }

        run = cfa_read_le16(src + src_off);
        rgb0 = src[src_off + 2U];
        rgb1 = src[src_off + 3U];
        alpha = src[src_off + 4U];
        src_off += 5U;

        if (run == 0U || px + run > pixel_count) {
            return ESP_ERR_INVALID_SIZE;
        }

        for (i = 0; i < run; ++i) {
            const size_t rgb_off = (px + i) * 2U;

            dst[rgb_off] = rgb0;
            dst[rgb_off + 1U] = rgb1;
            dst[rgb_size + px + i] = alpha;
        }
        px += run;
    }

    return px == pixel_count ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t cfa_resolve_payload(animation_cfa_decoder_t *decoder,
                                     const cfa_frame_record_t *record,
                                     const uint8_t **out_payload)
{
    if (!decoder || !record || !out_payload) {
        return ESP_ERR_INVALID_ARG;
    }

    if (record->payload_size == 0U) {
        *out_payload = NULL;
        return ESP_OK;
    }

    if (!decoder->file_data ||
        decoder->file_data_size != decoder->header.file_size) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_payload = decoder->file_data + record->payload_offset;
    return ESP_OK;
}

static esp_err_t cfa_preload_file_psram(const char *path, size_t file_size, uint8_t **out_data)
{
    FILE *fp = NULL;
    uint8_t *data = NULL;
    esp_err_t err = ESP_OK;

    if (!path || !out_data || file_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    data = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) {
        return ESP_ERR_NO_MEM;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        heap_caps_free(data);
        return ESP_ERR_NOT_FOUND;
    }

    err = cfa_read_exact_cooperative(fp, data, file_size);
    fclose(fp);
    if (err != ESP_OK) {
        heap_caps_free(data);
        return err;
    }

    *out_data = data;
    return ESP_OK;
}

static bool cfa_should_preload(size_t file_size, size_t working_buffer_size)
{
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    return file_size > 0U &&
           file_size <= CONFIG_FAIRYTALE_ANIM_CFA_CACHE_MAX_BYTES &&
           free_psram > file_size + working_buffer_size +
                            CONFIG_FAIRYTALE_ANIM_CFA_FREE_PSRAM_MARGIN_BYTES;
}

esp_err_t animation_cfa_decoder_open(const char *uri, animation_cfa_decoder_t **out_decoder)
{
    animation_cfa_decoder_t *decoder = NULL;
    FILE *fp = NULL;
    char path[256] = {0};
    uint8_t header_bytes[CFA_HEADER_SIZE] = {0};
    long file_size = 0;
    uint8_t *index_bytes = NULL;
    uint8_t *file_data = NULL;
    size_t index_size = 0;
    size_t i = 0;
    esp_err_t err = ESP_OK;

    if (!uri || !out_decoder) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_decoder = NULL;
    err = media_storage_ensure_ready_for_uri(uri);
    if (err != ESP_OK) {
        return err;
    }
    err = media_storage_resolve_path(uri, path, sizeof(path));
    if (err != ESP_OK) {
        return err;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return ESP_FAIL;
    }
    file_size = ftell(fp);
    if (file_size <= (long)CFA_HEADER_SIZE || (uint64_t)file_size > UINT32_MAX) {
        fclose(fp);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(fp);

    err = cfa_read_exact(fp, header_bytes, sizeof(header_bytes));
    if (err != ESP_OK) {
        fclose(fp);
        return err;
    }

    decoder = calloc(1, sizeof(*decoder));
    if (!decoder) {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    err = cfa_validate_header_bytes(header_bytes, &decoder->header);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "invalid CFA header uri=%s err=%s", uri, esp_err_to_name(err));
        fclose(fp);
        animation_cfa_decoder_close(decoder);
        return err;
    }

    if (decoder->header.file_size != (uint32_t)file_size ||
        decoder->header.payload_offset < decoder->header.index_offset ||
        decoder->header.payload_offset > (uint32_t)file_size) {
        fclose(fp);
        animation_cfa_decoder_close(decoder);
        return ESP_ERR_INVALID_SIZE;
    }

    if (decoder->header.frame_count > ((uint32_t)file_size - CFA_HEADER_SIZE) / CFA_FRAME_SIZE) {
        fclose(fp);
        animation_cfa_decoder_close(decoder);
        return ESP_ERR_INVALID_SIZE;
    }

    index_size = (size_t)decoder->header.frame_count * CFA_FRAME_SIZE;
    if (decoder->header.index_offset + index_size > decoder->header.payload_offset) {
        fclose(fp);
        animation_cfa_decoder_close(decoder);
        return ESP_ERR_INVALID_SIZE;
    }

    decoder->uri = strdup(uri);
    decoder->frames = calloc(decoder->header.frame_count, sizeof(*decoder->frames));
    index_bytes = malloc(index_size);
    if (!decoder->uri || !decoder->frames || !index_bytes) {
        fclose(fp);
        free(index_bytes);
        animation_cfa_decoder_close(decoder);
        return ESP_ERR_NO_MEM;
    }

    if (fseek(fp, (long)decoder->header.index_offset, SEEK_SET) != 0 ||
        cfa_read_exact(fp, index_bytes, index_size) != ESP_OK) {
        fclose(fp);
        free(index_bytes);
        animation_cfa_decoder_close(decoder);
        return ESP_FAIL;
    }
    fclose(fp);
    fp = NULL;

    for (i = 0; i < decoder->header.frame_count; ++i) {
        err = cfa_read_frame_record(index_bytes + (i * CFA_FRAME_SIZE), &decoder->frames[i]);
        if (err == ESP_OK) {
            err = cfa_validate_frame_record(&decoder->header, &decoder->frames[i], (size_t)file_size);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "invalid CFA frame %u uri=%s err=%s", (unsigned)i, uri, esp_err_to_name(err));
            free(index_bytes);
            animation_cfa_decoder_close(decoder);
            return err;
        }
    }
    free(index_bytes);

    decoder->rect_buf_capacity = decoder->header.max_rect_pixels * 3U;
    if (!cfa_should_preload((size_t)file_size, decoder->rect_buf_capacity)) {
        ESP_LOGW(TAG,
                 "CFA preload requires more PSRAM uri=%s size=%u free=%u margin=%u",
                 uri,
                 (unsigned)file_size,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)CONFIG_FAIRYTALE_ANIM_CFA_FREE_PSRAM_MARGIN_BYTES);
        animation_cfa_decoder_close(decoder);
        return ESP_ERR_NO_MEM;
    }

    err = cfa_preload_file_psram(path, (size_t)file_size, &file_data);
    if (err != ESP_OK) {
        animation_cfa_decoder_close(decoder);
        return err;
    }
    decoder->file_data = file_data;
    decoder->file_data_size = (size_t)file_size;

    /* Frame payloads always point directly into the complete CFA image. */
    if (decoder->rect_buf_capacity > 0U) {
        decoder->rect_buf = cfa_malloc(decoder->rect_buf_capacity);
    }
    if (decoder->rect_buf_capacity > 0U && !decoder->rect_buf) {
        animation_cfa_decoder_close(decoder);
        return ESP_ERR_NO_MEM;
    }

    decoder->next_frame = 0U;
    decoder->current_frame = 0U;
    decoder->frame_ready = false;
    *out_decoder = decoder;

    ESP_LOGI(TAG,
             "opened cfa uri=%s size=%u canvas=%ux%u frames=%u preload=%u max_payload=%u max_rect_px=%u",
             uri,
             (unsigned)file_size,
             (unsigned)decoder->header.canvas_width,
             (unsigned)decoder->header.canvas_height,
             (unsigned)decoder->header.frame_count,
             decoder->file_data ? 1U : 0U,
             (unsigned)decoder->header.max_payload_size,
             (unsigned)decoder->header.max_rect_pixels);
    return ESP_OK;
}

void animation_cfa_decoder_close(animation_cfa_decoder_t *decoder)
{
    if (!decoder) {
        return;
    }

    free(decoder->uri);
    free(decoder->frames);
    if (decoder->file_data) {
        heap_caps_free(decoder->file_data);
    }
    if (decoder->rect_buf) {
        heap_caps_free(decoder->rect_buf);
    }
    free(decoder);
}

esp_err_t animation_cfa_decoder_rewind(animation_cfa_decoder_t *decoder)
{
    if (!decoder) {
        return ESP_ERR_INVALID_ARG;
    }

    decoder->next_frame = 0U;
    decoder->current_frame = 0U;
    decoder->frame_ready = false;
    return ESP_OK;
}

int animation_cfa_decoder_decode_next(animation_cfa_decoder_t *decoder)
{
    const cfa_frame_record_t *record = NULL;
    const uint8_t *payload = NULL;
    uint32_t payload_crc = 0;
    size_t pixel_count = 0;
    uint64_t frame_started = 0;
    uint64_t step_started = 0;
    esp_err_t err = ESP_OK;

    if (!decoder || !decoder->frames) {
        return -1;
    }

    memset(&decoder->last_profile, 0, sizeof(decoder->last_profile));
    frame_started = (uint64_t)esp_timer_get_time();

    if (decoder->next_frame >= decoder->header.frame_count) {
        decoder->frame_ready = false;
        return 0;
    }

    record = &decoder->frames[decoder->next_frame];
    decoder->current_frame = decoder->next_frame;
    decoder->next_frame++;
    decoder->frame_ready = false;
    decoder->last_profile.frame_index = (uint32_t)decoder->current_frame;
    decoder->last_profile.frame_width = record->w;
    decoder->last_profile.frame_height = record->h;
    decoder->last_profile.codec = record->codec;
    decoder->last_profile.preloaded = animation_cfa_decoder_is_preloaded(decoder);
    decoder->last_profile.rect_pixels = (size_t)record->w * (size_t)record->h;
    decoder->last_profile.raw_size = record->raw_size;
    decoder->last_profile.payload_size = record->payload_size;

    if ((record->flags & ANIMATION_CFA_FRAME_EMPTY) != 0U) {
        decoder->frame_ready = true;
        decoder->last_profile.total_us = (uint64_t)esp_timer_get_time() - frame_started;
        return 1;
    }

    step_started = (uint64_t)esp_timer_get_time();
    err = cfa_resolve_payload(decoder, record, &payload);
    decoder->last_profile.read_us = (uint64_t)esp_timer_get_time() - step_started;
    if (err != ESP_OK || !payload) {
        return -1;
    }

#if CONFIG_FAIRYTALE_ANIM_CFA_VALIDATE_PAYLOAD_CRC
    step_started = (uint64_t)esp_timer_get_time();
    payload_crc = cfa_crc32(payload, record->payload_size);
    decoder->last_profile.crc_us = (uint64_t)esp_timer_get_time() - step_started;
    if (payload_crc != record->payload_crc32) {
        ESP_LOGE(TAG, "CFA payload CRC mismatch frame=%u", (unsigned)decoder->current_frame);
        return -1;
    }
#else
    (void)payload_crc;
#endif

    pixel_count = (size_t)record->w * (size_t)record->h;
    if (record->raw_size > decoder->rect_buf_capacity || !decoder->rect_buf) {
        return -1;
    }

    step_started = (uint64_t)esp_timer_get_time();
    switch (record->codec) {
    case CFA_CODEC_RAW:
        if (record->payload_size != record->raw_size) {
            return -1;
        }
        memcpy(decoder->rect_buf, payload, record->raw_size);
        break;

    case CFA_CODEC_RLE24:
        if (cfa_decode_rle24(payload, record->payload_size, decoder->rect_buf, record->raw_size, pixel_count) != ESP_OK) {
            return -1;
        }
        break;

    case CFA_CODEC_LZ4_BLOCK:
        if (lz4_block_decompress_safe(payload, record->payload_size, decoder->rect_buf, record->raw_size) !=
            (int)record->raw_size) {
            return -1;
        }
        break;

    default:
        return -1;
    }
    decoder->last_profile.decompress_us = (uint64_t)esp_timer_get_time() - step_started;

    decoder->frame_ready = true;
    decoder->last_profile.total_us = (uint64_t)esp_timer_get_time() - frame_started;
    return 1;
}

esp_err_t animation_cfa_decoder_canvas_info(const animation_cfa_decoder_t *decoder,
                                            uint16_t *out_width,
                                            uint16_t *out_height)
{
    if (!decoder || !out_width || !out_height) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_width = decoder->header.canvas_width;
    *out_height = decoder->header.canvas_height;
    return ESP_OK;
}

esp_err_t animation_cfa_decoder_frame_view(const animation_cfa_decoder_t *decoder,
                                           animation_cfa_frame_view_t *out_view)
{
    const cfa_frame_record_t *record = NULL;
    size_t pixel_count = 0;

    if (!decoder || !out_view || !decoder->frame_ready || decoder->current_frame >= decoder->header.frame_count) {
        return ESP_ERR_INVALID_STATE;
    }

    record = &decoder->frames[decoder->current_frame];
    pixel_count = (size_t)record->w * (size_t)record->h;

    memset(out_view, 0, sizeof(*out_view));
    out_view->canvas_width = decoder->header.canvas_width;
    out_view->canvas_height = decoder->header.canvas_height;
    out_view->frame_x = record->x;
    out_view->frame_y = record->y;
    out_view->frame_width = record->w;
    out_view->frame_height = record->h;
    out_view->delay_ms = record->delay_ms;
    out_view->flags = record->flags;
    out_view->pixel_count = pixel_count;
    if ((record->flags & ANIMATION_CFA_FRAME_EMPTY) == 0U && decoder->rect_buf) {
        out_view->rgb565 = decoder->rect_buf;
        out_view->alpha = decoder->rect_buf + (pixel_count * 2U);
    }

    return ESP_OK;
}

bool animation_cfa_decoder_frame_ready(const animation_cfa_decoder_t *decoder)
{
    return decoder && decoder->frame_ready;
}

uint32_t animation_cfa_decoder_frame_delay_ms(const animation_cfa_decoder_t *decoder)
{
    if (!decoder || !decoder->frame_ready || decoder->current_frame >= decoder->header.frame_count) {
        return 30U;
    }

    return decoder->frames[decoder->current_frame].delay_ms ? decoder->frames[decoder->current_frame].delay_ms : 30U;
}

size_t animation_cfa_decoder_file_size(const animation_cfa_decoder_t *decoder)
{
    return decoder ? decoder->header.file_size : 0U;
}

bool animation_cfa_decoder_is_preloaded(const animation_cfa_decoder_t *decoder)
{
    return decoder && decoder->file_data && decoder->file_data_size == decoder->header.file_size;
}

esp_err_t animation_cfa_decoder_last_profile(const animation_cfa_decoder_t *decoder,
                                             animation_cfa_frame_profile_t *out_profile)
{
    if (!decoder || !out_profile) {
        return ESP_ERR_INVALID_ARG;
    }
    if (decoder->last_profile.total_us == 0U && !decoder->frame_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_profile = decoder->last_profile;
    return ESP_OK;
}
