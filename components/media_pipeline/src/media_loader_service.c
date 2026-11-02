#include "media_loader_service.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define MEDIA_LOADER_MAX_PENDING 16U
#define MEDIA_LOADER_TASK_STACK  20480U
#define MEDIA_LOADER_TASK_PRIO   2U

typedef struct
{
    bool in_use;
    uint32_t order;
    media_loader_request_t request;
} media_loader_slot_t;

typedef struct
{
    SemaphoreHandle_t lock;
    SemaphoreHandle_t wake_sem;
    TaskHandle_t task;
    uint32_t next_order;
    bool initialized;
    media_loader_slot_t slots[MEDIA_LOADER_MAX_PENDING];
} media_loader_state_t;

static const char *TAG = "media_loader";
static media_loader_state_t s_state = {0};

static int media_loader_priority_value(media_loader_priority_t priority)
{
    return (int)priority;
}

static void media_loader_release_request(media_loader_request_t *request)
{
    if (!request) {
        return;
    }

    if (request->free_cb && request->ctx) {
        request->free_cb(request->ctx);
    }
    memset(request, 0, sizeof(*request));
}

static bool media_loader_take_next_request(media_loader_request_t *out_request)
{
    media_loader_slot_t *candidate = NULL;
    size_t i = 0;

    if (!out_request || !s_state.lock) {
        return false;
    }

    if (xSemaphoreTake(s_state.lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    for (i = 0; i < MEDIA_LOADER_MAX_PENDING; ++i) {
        media_loader_slot_t *slot = &s_state.slots[i];

        if (!slot->in_use) {
            continue;
        }

        if (!candidate ||
            media_loader_priority_value(slot->request.priority) < media_loader_priority_value(candidate->request.priority) ||
            (slot->request.priority == candidate->request.priority && slot->order < candidate->order)) {
            candidate = slot;
        }
    }

    if (!candidate) {
        xSemaphoreGive(s_state.lock);
        return false;
    }

    *out_request = candidate->request;
    memset(candidate, 0, sizeof(*candidate));

    xSemaphoreGive(s_state.lock);
    return true;
}

static void media_loader_task(void *arg)
{
    (void)arg;

    for (;;) {
        media_loader_request_t request = {0};
        bool stale = false;
        bool ownership_taken = false;

        if (xSemaphoreTake(s_state.wake_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        while (media_loader_take_next_request(&request)) {
            stale = request.stale_cb ? request.stale_cb(request.ctx, request.generation) : false;
            if (!stale && request.run_cb) {
                ownership_taken = request.run_cb(request.ctx, request.generation);
            } else {
                ownership_taken = false;
            }

            if (!ownership_taken) {
                media_loader_release_request(&request);
            } else {
                memset(&request, 0, sizeof(request));
            }
        }
    }
}

esp_err_t media_loader_service_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }

    s_state.lock = xSemaphoreCreateMutex();
    s_state.wake_sem = xSemaphoreCreateBinary();
    if (!s_state.lock || !s_state.wake_sem) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(media_loader_task, "media_loader", MEDIA_LOADER_TASK_STACK, NULL, MEDIA_LOADER_TASK_PRIO, &s_state.task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_state.initialized = true;
    ESP_LOGI(TAG, "media loader initialized");
    return ESP_OK;
}

bool media_loader_service_is_ready(void)
{
    return s_state.initialized;
}

esp_err_t media_loader_service_enqueue(const media_loader_request_t *request)
{
    size_t i = 0;
    media_loader_slot_t *free_slot = NULL;

    if (!request || !request->run_cb) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_state.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (i = 0; i < MEDIA_LOADER_MAX_PENDING; ++i) {
        if (!s_state.slots[i].in_use) {
            free_slot = &s_state.slots[i];
            break;
        }
    }

    if (!free_slot) {
        xSemaphoreGive(s_state.lock);
        return ESP_ERR_NO_MEM;
    }

    free_slot->in_use = true;
    free_slot->order = ++s_state.next_order;
    free_slot->request = *request;

    xSemaphoreGive(s_state.lock);
    xSemaphoreGive(s_state.wake_sem);
    return ESP_OK;
}
