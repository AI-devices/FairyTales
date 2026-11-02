#include "settings_store.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings_store";
static const char *kProfileNs = "profile";
static const char *kMetaNs = "meta";
static const char *kUserNameKey = "user_name";
static const char *kVolumeKey = "volume";
static const char *kBrightnessKey = "brightness";
static const char *kModeHintHiddenKey = "mode_hint_off";
static const char *kSchemaVersionKey = "schema_version";

static SemaphoreHandle_t s_lock = NULL;
static bool s_initialized = false;

static esp_err_t settings_store_lock(void)
{
    if (!s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void settings_store_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static esp_err_t ensure_schema_version_locked(void)
{
    nvs_handle_t meta = 0;
    ESP_RETURN_ON_ERROR(nvs_open(kMetaNs, NVS_READWRITE, &meta), TAG, "open meta namespace failed");

    uint16_t version = 0;
    esp_err_t err = nvs_get_u16(meta, kSchemaVersionKey, &version);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        version = SETTINGS_STORE_SCHEMA_VERSION;
        ESP_RETURN_ON_ERROR(nvs_set_u16(meta, kSchemaVersionKey, version), TAG, "set schema version failed");
        ESP_RETURN_ON_ERROR(nvs_commit(meta), TAG, "commit schema version failed");
        err = ESP_OK;
    } else if (err == ESP_OK) {
        if (version == 0) {
            version = SETTINGS_STORE_SCHEMA_VERSION;
            ESP_RETURN_ON_ERROR(nvs_set_u16(meta, kSchemaVersionKey, version), TAG, "repair schema version failed");
            ESP_RETURN_ON_ERROR(nvs_commit(meta), TAG, "commit repaired schema version failed");
        }
    }

    nvs_close(meta);
    return err;
}

static esp_err_t settings_store_get_i32_locked(const char *ns,
                                               const char *key,
                                               int32_t *out_value,
                                               bool *out_found)
{
    nvs_handle_t handle = 0;
    esp_err_t err = ESP_OK;

    if (out_found) {
        *out_found = false;
    }

    err = nvs_open(ns, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_i32(handle, key, out_value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    } else if (err == ESP_OK && out_found) {
        *out_found = true;
    }

    nvs_close(handle);
    return err;
}

static esp_err_t settings_store_set_i32_locked(const char *ns, const char *key, int32_t value)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t settings_store_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");
    esp_err_t err = ensure_schema_version_locked();
    settings_store_unlock();
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "settings store initialized");
    return ESP_OK;
}

esp_err_t settings_store_get_schema_version(uint16_t *out_version)
{
    ESP_RETURN_ON_FALSE(out_version != NULL, ESP_ERR_INVALID_ARG, TAG, "out_version is NULL");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings store not initialized");
    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");

    nvs_handle_t meta = 0;
    esp_err_t err = nvs_open(kMetaNs, NVS_READONLY, &meta);
    if (err == ESP_OK) {
        err = nvs_get_u16(meta, kSchemaVersionKey, out_version);
        nvs_close(meta);
    }

    settings_store_unlock();
    return err;
}

esp_err_t settings_store_get_user_name(char *buf, size_t buf_size, bool *out_found)
{
    ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_INVALID_ARG, TAG, "buf is NULL");
    ESP_RETURN_ON_FALSE(buf_size > 0, ESP_ERR_INVALID_ARG, TAG, "buf_size is 0");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings store not initialized");

    buf[0] = '\0';
    if (out_found) {
        *out_found = false;
    }

    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");

    nvs_handle_t profile = 0;
    esp_err_t err = nvs_open(kProfileNs, NVS_READONLY, &profile);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        settings_store_unlock();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        settings_store_unlock();
        return err;
    }

    size_t required = buf_size;
    err = nvs_get_str(profile, kUserNameKey, buf, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    } else if (err == ESP_OK && out_found) {
        *out_found = buf[0] != '\0';
    }

    nvs_close(profile);
    settings_store_unlock();
    return err;
}

esp_err_t settings_store_set_user_name(const char *name)
{
    ESP_RETURN_ON_FALSE(name != NULL, ESP_ERR_INVALID_ARG, TAG, "name is NULL");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings store not initialized");

    char clamped[SETTINGS_STORE_USER_NAME_MAX_LEN + 1] = {0};
    size_t len = strnlen(name, SETTINGS_STORE_USER_NAME_MAX_LEN);
    memcpy(clamped, name, len);
    clamped[len] = '\0';

    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");

    nvs_handle_t profile = 0;
    esp_err_t err = nvs_open(kProfileNs, NVS_READWRITE, &profile);
    if (err == ESP_OK) {
        err = nvs_set_str(profile, kUserNameKey, clamped);
        if (err == ESP_OK) {
            err = nvs_commit(profile);
        }
        nvs_close(profile);
    }

    settings_store_unlock();
    return err;
}

esp_err_t settings_store_get_volume(int32_t *out_value, bool *out_found)
{
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(out_value != NULL, ESP_ERR_INVALID_ARG, TAG, "out_value is NULL");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings store not initialized");

    *out_value = 0;
    if (out_found) {
        *out_found = false;
    }

    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");
    err = settings_store_get_i32_locked(kProfileNs, kVolumeKey, out_value, out_found);
    settings_store_unlock();
    return err;
}

esp_err_t settings_store_set_volume(int32_t value)
{
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings store not initialized");

    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }

    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");
    err = settings_store_set_i32_locked(kProfileNs, kVolumeKey, value);
    settings_store_unlock();
    return err;
}

esp_err_t settings_store_get_display_brightness(int32_t *out_value, bool *out_found)
{
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(out_value != NULL, ESP_ERR_INVALID_ARG, TAG, "out_value is NULL");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings store not initialized");

    *out_value = 0;
    if (out_found) {
        *out_found = false;
    }

    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");
    err = settings_store_get_i32_locked(kProfileNs, kBrightnessKey, out_value, out_found);
    settings_store_unlock();
    return err;
}

esp_err_t settings_store_set_display_brightness(int32_t value)
{
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings store not initialized");

    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }

    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");
    err = settings_store_set_i32_locked(kProfileNs, kBrightnessKey, value);
    settings_store_unlock();
    return err;
}

esp_err_t settings_store_get_mode_hint_hidden(bool *out_hidden)
{
    int32_t hidden = 0;
    bool found = false;
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(out_hidden != NULL, ESP_ERR_INVALID_ARG, TAG, "out_hidden is NULL");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings store not initialized");

    *out_hidden = false;
    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");
    err = settings_store_get_i32_locked(kProfileNs, kModeHintHiddenKey, &hidden, &found);
    settings_store_unlock();
    if (err == ESP_OK && found) {
        *out_hidden = hidden != 0;
    }
    return err;
}

esp_err_t settings_store_set_mode_hint_hidden(bool hidden)
{
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings store not initialized");
    ESP_RETURN_ON_ERROR(settings_store_lock(), TAG, "settings store lock failed");
    err = settings_store_set_i32_locked(kProfileNs, kModeHintHiddenKey, hidden ? 1 : 0);
    settings_store_unlock();
    return err;
}
