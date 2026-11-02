#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_STORE_SCHEMA_VERSION 1
#define SETTINGS_STORE_USER_NAME_MAX_LEN 63

esp_err_t settings_store_init(void);

esp_err_t settings_store_get_schema_version(uint16_t *out_version);

esp_err_t settings_store_get_user_name(char *buf, size_t buf_size, bool *out_found);
esp_err_t settings_store_set_user_name(const char *name);
esp_err_t settings_store_get_volume(int32_t *out_value, bool *out_found);
esp_err_t settings_store_set_volume(int32_t value);
esp_err_t settings_store_get_display_brightness(int32_t *out_value, bool *out_found);
esp_err_t settings_store_set_display_brightness(int32_t value);
esp_err_t settings_store_get_mode_hint_hidden(bool *out_hidden);
esp_err_t settings_store_set_mode_hint_hidden(bool hidden);

#ifdef __cplusplus
}
#endif
