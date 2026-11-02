#include "media_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "bsp/esp32_p4_function_ev_board.h"

static const char *TAG = "media_storage";
static bool s_sd_mount_attempted = false;
static bool s_sd_ready = false;

bool media_storage_is_uri(const char *path_or_uri)
{
    return path_or_uri &&
           (strncmp(path_or_uri, "spiffs:/", 8) == 0 ||
            strncmp(path_or_uri, "sd:/", 4) == 0 ||
            strncmp(path_or_uri, "S:/", 3) == 0);
}

esp_err_t media_storage_ensure_ready_for_uri(const char *path_or_uri)
{
    const bool is_sd_uri = path_or_uri && strncmp(path_or_uri, "sd:/", 4) == 0;
    const bool is_sd_path = path_or_uri && strncmp(path_or_uri, "/sdcard/", 8) == 0;

    if (!path_or_uri) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!is_sd_uri && !is_sd_path) {
        return ESP_OK;
    }

    return media_storage_mount_sdcard();
}

esp_err_t media_storage_mount_sdcard(void)
{
    if (s_sd_ready) {
        return ESP_OK;
    }

    if (!s_sd_mount_attempted) {
        esp_err_t err = bsp_sdcard_mount();

        s_sd_mount_attempted = true;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(err));
            return err;
        }
        s_sd_ready = true;
        ESP_LOGI(TAG, "SD storage mounted at %s", BSP_SD_MOUNT_POINT);
    }

    return s_sd_ready ? ESP_OK : ESP_FAIL;
}

esp_err_t media_storage_resolve_path(const char *path_or_uri, char *out_path, size_t out_path_size)
{
    const char *suffix = NULL;

    if (!path_or_uri || !out_path || out_path_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strncmp(path_or_uri, "spiffs:/", 8) == 0) {
        suffix = path_or_uri + 8;
        snprintf(out_path, out_path_size, "/spiffs/%s", suffix);
        return ESP_OK;
    }

    if (strncmp(path_or_uri, "sd:/", 4) == 0) {
        suffix = path_or_uri + 4;
        snprintf(out_path, out_path_size, "%s/%s", BSP_SD_MOUNT_POINT, suffix);
        return ESP_OK;
    }

    if (strncmp(path_or_uri, "S:/", 3) == 0) {
        suffix = path_or_uri + 3;
        snprintf(out_path, out_path_size, "/spiffs/%s", suffix);
        return ESP_OK;
    }

    snprintf(out_path, out_path_size, "%s", path_or_uri);
    return ESP_OK;
}

esp_err_t media_storage_read_all(const char *path_or_uri,
                                 uint8_t **out_data,
                                 size_t *out_size,
                                 uint32_t caps)
{
    char path[256] = {0};
    FILE *fp = NULL;
    long file_size = 0;
    uint8_t *data = NULL;
    size_t read_now = 0;

    if (!path_or_uri || !out_data || !out_size) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(media_storage_ensure_ready_for_uri(path_or_uri), TAG, "storage not ready");
    ESP_RETURN_ON_ERROR(media_storage_resolve_path(path_or_uri, path, sizeof(path)), TAG, "resolve path failed");

    fp = fopen(path, "rb");
    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return ESP_FAIL;
    }

    file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(fp);

    data = heap_caps_malloc((size_t)file_size, caps ? caps : MALLOC_CAP_8BIT);
    if (!data) {
        data = heap_caps_malloc((size_t)file_size, MALLOC_CAP_8BIT);
    }
    if (!data) {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    read_now = fread(data, 1, (size_t)file_size, fp);
    fclose(fp);

    if (read_now != (size_t)file_size) {
        heap_caps_free(data);
        return ESP_FAIL;
    }

    *out_data = data;
    *out_size = (size_t)file_size;
    return ESP_OK;
}
