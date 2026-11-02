#include "ui_dispatch.h"

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"

#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "lvgl.h"

typedef struct
{
    ui_dispatch_job_cb_t cb;
    void *ctx;
    ui_dispatch_free_cb_t free_cb;
    SemaphoreHandle_t sem;
} ui_dispatch_job_t;

typedef struct
{
    QueueHandle_t queue;
    lv_timer_t *timer;
    bool initialized;
} ui_dispatch_state_t;

static const char *TAG = "ui_dispatch";
static ui_dispatch_state_t s_state = {0};

static void ui_dispatch_drain_cb(lv_timer_t *timer)
{
    ui_dispatch_state_t *state = (ui_dispatch_state_t *)timer->user_data;
    ui_dispatch_job_t job = {0};
    while (xQueueReceive(state->queue, &job, 0) == pdTRUE) {
        if (job.cb) {
            job.cb(job.ctx);
        }
        if (job.free_cb) {
            job.free_cb(job.ctx);
        }
        if (job.sem) {
            xSemaphoreGive(job.sem);
        }
    }
}

esp_err_t ui_dispatch_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }

    s_state.queue = xQueueCreate(32, sizeof(ui_dispatch_job_t));
    if (!s_state.queue) {
        return ESP_ERR_NO_MEM;
    }

    s_state.timer = lv_timer_create(ui_dispatch_drain_cb, 10, &s_state);
    if (!s_state.timer) {
        vQueueDelete(s_state.queue);
        s_state.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_state.initialized = true;
    ESP_LOGI(TAG, "ui dispatch initialized");
    return ESP_OK;
}

bool ui_dispatch_is_ready(void)
{
    return s_state.initialized;
}

esp_err_t ui_dispatch_post(ui_dispatch_job_cb_t cb,
                           void *ctx,
                           ui_dispatch_free_cb_t free_cb,
                           bool async,
                           TickType_t timeout_ticks)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "ui dispatch not initialized");
    ESP_RETURN_ON_FALSE(cb != NULL, ESP_ERR_INVALID_ARG, TAG, "callback is NULL");

    SemaphoreHandle_t sem = NULL;
    if (!async) {
        sem = xSemaphoreCreateBinary();
        if (!sem) {
            return ESP_ERR_NO_MEM;
        }
    }

    ui_dispatch_job_t job = {
        .cb = cb,
        .ctx = ctx,
        .free_cb = free_cb,
        .sem = sem,
    };

    esp_err_t err = ESP_FAIL;
    if (xQueueSend(s_state.queue, &job, timeout_ticks) == pdTRUE) {
        if (sem) {
            err = xSemaphoreTake(sem, timeout_ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
        } else {
            err = ESP_OK;
        }
    }

    if (sem) {
        vSemaphoreDelete(sem);
    }

    return err;
}
