#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_dispatch_job_cb_t)(void *ctx);
typedef void (*ui_dispatch_free_cb_t)(void *ctx);

esp_err_t ui_dispatch_init(void);
bool ui_dispatch_is_ready(void);

esp_err_t ui_dispatch_post(ui_dispatch_job_cb_t cb,
                           void *ctx,
                           ui_dispatch_free_cb_t free_cb,
                           bool async,
                           TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif
