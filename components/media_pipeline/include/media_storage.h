#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool media_storage_is_uri(const char *path_or_uri);
esp_err_t media_storage_mount_sdcard(void);
esp_err_t media_storage_ensure_ready_for_uri(const char *path_or_uri);
esp_err_t media_storage_resolve_path(const char *path_or_uri, char *out_path, size_t out_path_size);
esp_err_t media_storage_read_all(const char *path_or_uri,
                                 uint8_t **out_data,
                                 size_t *out_size,
                                 uint32_t caps);

#ifdef __cplusplus
}
#endif
