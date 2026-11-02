#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "miniz.h"

#include "media_storage.h"
#include "ui_img_manager.h"

static const char *TAG = "UIIMG";

uint8_t* _ui_load_binary_direct(const char* fname_S, uint32_t size)
{
    if (!fname_S || size == 0) {
        ESP_LOGE(TAG, "bad args: fname_S=%p size=%u",
                 (void*)fname_S, (unsigned)size);
        return NULL;
    }

    uint64_t t_start_us = esp_timer_get_time();  

    char real[256];
    if (media_storage_ensure_ready_for_uri(fname_S) != ESP_OK ||
        media_storage_resolve_path(fname_S, real, sizeof(real)) != ESP_OK) {
        ESP_LOGE(TAG, "unable to resolve media path: %s", fname_S ? fname_S : "<null>");
        return NULL;
    }

    ESP_LOGD(TAG, "load image: %s (uncompressed=%u bytes)",
             real, (unsigned)size);

    FILE *fp = fopen(real, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "fopen failed: %s", real);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        ESP_LOGE(TAG, "fseek(SEEK_END) failed: %s", real);
        fclose(fp);
        return NULL;
    }

    long file_size_long = ftell(fp);
    if (file_size_long <= 0) {
        ESP_LOGE(TAG, "ftell failed or empty file: %s", real);
        fclose(fp);
        return NULL;
    }

    size_t file_size = (size_t)file_size_long;
    rewind(fp);

    if (file_size == size) {
        ESP_LOGD(TAG, "image is RAW (%u bytes), reading directly", (unsigned)file_size);

        uint8_t *buf = (uint8_t*)heap_caps_malloc(size,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) {
            ESP_LOGE(TAG, "heap_caps_malloc(%u) failed", (unsigned)size);
            fclose(fp);
            return NULL;
        }

        size_t rd = fread(buf, 1, size, fp);
        fclose(fp);

        if (rd != size) {
            ESP_LOGE(TAG, "fread short: %u/%u", (unsigned)rd, (unsigned)size);
            heap_caps_free(buf);
            return NULL;
        }

        uint64_t t_end_us = esp_timer_get_time();
        ESP_LOGD(TAG,
                 "load RAW image done: %s (flash=%u, out=%u) in %llu ms",
                 real,
                 (unsigned)file_size,
                 (unsigned)size,
                 (unsigned long long)((t_end_us - t_start_us) / 1000ULL));

        return buf;
    }

    ESP_LOGD(TAG,
             "image assumed COMPRESSED (file=%u bytes, uncompressed=%u)",
             (unsigned)file_size, (unsigned)size);

    uint8_t *comp_buf = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_8BIT);
    if (!comp_buf) {
        ESP_LOGE(TAG, "heap_caps_malloc(comp_buf, %u) failed", (unsigned)file_size);
        fclose(fp);
        return NULL;
    }

    size_t rd = fread(comp_buf, 1, file_size, fp);
    fclose(fp);

    if (rd != file_size) {
        ESP_LOGE(TAG, "fread short (compressed): %u/%u",
                 (unsigned)rd, (unsigned)file_size);
        heap_caps_free(comp_buf);
        return NULL;
    }

    uint8_t *out_buf = (uint8_t*)heap_caps_malloc(size,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out_buf) {
        ESP_LOGE(TAG, "heap_caps_malloc(out_buf, %u) failed", (unsigned)size);
        heap_caps_free(comp_buf);
        return NULL;
    }

    size_t dst_len = tinfl_decompress_mem_to_mem(
        out_buf,
        size,
        comp_buf,
        file_size,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    heap_caps_free(comp_buf);

    if (dst_len == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        ESP_LOGE(TAG, "tinfl_decompress_mem_to_mem() failed");
        heap_caps_free(out_buf);
        return NULL;
    }

    if (dst_len != size) {
        ESP_LOGE(TAG, "decompress size mismatch: got=%lu expected=%u",
                 (unsigned long)dst_len, (unsigned)size);
        heap_caps_free(out_buf);
        return NULL;
    }

    uint64_t t_end_us = esp_timer_get_time();
    ESP_LOGD(TAG,
             "load COMPRESSED image done: %s (flash=%u, out=%u) in %llu ms",
             real,
             (unsigned)file_size,
             (unsigned)size,
             (unsigned long long)((t_end_us - t_start_us) / 1000ULL));

    ESP_LOGD(TAG, "image decompressed OK: %s", real);
    return out_buf;
}
