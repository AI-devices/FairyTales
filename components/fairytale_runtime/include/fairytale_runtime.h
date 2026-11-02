#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fairytale_runtime_progress_cb_t)(uint8_t progress_percent,
                                                const char *stage,
                                                void *user_ctx);

esp_err_t fairytale_runtime_init(fairytale_runtime_progress_cb_t progress_cb,
                                 void *user_ctx);

#ifdef __cplusplus
}
#endif
