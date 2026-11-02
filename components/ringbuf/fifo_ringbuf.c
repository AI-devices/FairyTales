#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "string.h"

#include "fifo_ringbuf.h"

struct fifo_ringbuf_t
{
    void* buffer;
    size_t* item_sizes;
    size_t len;
    size_t item_size;
    size_t discarded_items_;
    UBaseType_t write_idx;             // Next position to write
    UBaseType_t read_idx;              // Next position to read
    UBaseType_t count;                 // Number of items currently in buffer
    SemaphoreHandle_t mutex;           // Thread safety
    SemaphoreHandle_t data_available;  // Counting semaphore for available data
    SemaphoreHandle_t space_available; // Counting semaphore for free space
};

// Initialize the ring buffer
fifo_ringbuf_t* fifo_ringbuf_init(size_t len, size_t item_size)
{
    fifo_ringbuf_t* rb = (fifo_ringbuf_t*)malloc(sizeof(fifo_ringbuf_t));
    if (! rb)
        return NULL;
    rb->buffer = malloc(item_size * len);
    if (! rb->buffer) {
        free(rb);
        return NULL;
    }
    rb->item_sizes = (size_t*)malloc(sizeof(size_t) * len);
    if (! rb->item_sizes) {
        free(rb->buffer);
        free(rb);
        return NULL;
    }
    rb->len       = len;
    rb->item_size = item_size;

    rb->write_idx        = 0;
    rb->read_idx         = 0;
    rb->count            = 0;
    rb->discarded_items_ = 0;

    rb->mutex = xSemaphoreCreateMutex();
    if (! rb->mutex) {
        free(rb->buffer);
        free(rb->item_sizes);
        free(rb);
        return NULL;
    }
    rb->data_available = xSemaphoreCreateCounting(rb->len, 0);
    if (! rb->data_available) {
        vSemaphoreDelete(rb->mutex);
        free(rb->buffer);
        free(rb->item_sizes);
        free(rb);
        return NULL;
    }
    rb->space_available = xSemaphoreCreateCounting(rb->len, rb->len);
    if (! rb->space_available) {
        vSemaphoreDelete(rb->data_available);
        vSemaphoreDelete(rb->mutex);
        free(rb->buffer);
        free(rb->item_sizes);
        free(rb);
        return NULL;
    }
    return rb;
}

void fifo_ringbuf_release(fifo_ringbuf_t* rb)
{
    if (rb) {
        if (rb->buffer)
            free(rb->buffer);
        if (rb->item_sizes)
            free(rb->item_sizes);
        vSemaphoreDelete(rb->space_available);
        vSemaphoreDelete(rb->data_available);
        vSemaphoreDelete(rb->mutex);
        free(rb);
    }
}

// Write data to the buffer (wait for available space)
size_t fifo_ringbuf_write(fifo_ringbuf_t* rb, const void* data, size_t size, size_t timeout)
{
    if (size > rb->item_size)
        return 0;

    // Wait until at least one slot is free (blocks if full)
    if (xSemaphoreTake(rb->space_available, timeout) != pdTRUE) {
        return 0;
    }

    // Now safe to proceed — we have reserved a slot
    xSemaphoreTake(rb->mutex, portMAX_DELAY);

    void* dst = (int8_t*)rb->buffer + rb->write_idx * rb->item_size;
    memcpy(dst, data, size);
    rb->item_sizes[rb->write_idx] = size;

    rb->write_idx = (rb->write_idx + 1) % rb->len;
    rb->count++; // Since we waited for space, count always < len before increment

    // Signal that new data is available for consumers
    xSemaphoreGive(rb->data_available);

    xSemaphoreGive(rb->mutex);

    return size;
}

// Read oldest unread packet (returns true if data was read)
size_t fifo_ringbuf_read(fifo_ringbuf_t* rb, void* data, size_t max_len, size_t timeout)
{
    // Block until a packet is available.
    if (xSemaphoreTake(rb->data_available, timeout) != pdTRUE) {
        return 0;
    }

    xSemaphoreTake(rb->mutex, portMAX_DELAY);

    void* item_data       = (int8_t*)rb->buffer + rb->read_idx * rb->item_size;
    size_t item_size      = rb->item_sizes[rb->read_idx];
    const size_t copy_len = (item_size < max_len) ? item_size : max_len;
    memcpy(data, item_data, copy_len);

    rb->read_idx = (rb->read_idx + 1) % rb->len;
    rb->count--;

    xSemaphoreGive(rb->space_available);
    xSemaphoreGive(rb->mutex);
    return copy_len;
}

static bool _fifo_ringbuf_discard(fifo_ringbuf_t* rb)
{
    if (xSemaphoreTake(rb->data_available, 0) != pdTRUE) {
        return false; // nothing to discard
    }

    xSemaphoreTake(rb->mutex, portMAX_DELAY);
    rb->read_idx = (rb->read_idx + 1) % rb->len;
    rb->count--;
    xSemaphoreGive(rb->mutex);

    xSemaphoreGive(rb->space_available);
    return true;
}

int fifo_ringbuf_reset(fifo_ringbuf_t* rb)
{
    if (! rb)
        return -1;

    // Step 1: Drain all currently available items.
    while (_fifo_ringbuf_discard(rb)) {
    }

    // Step 2: Reset indices under mutex
    xSemaphoreTake(rb->mutex, portMAX_DELAY);
    rb->read_idx = rb->write_idx = 0;
    rb->count                    = 0;
    xSemaphoreGive(rb->mutex);

    return 0;
}

int fifo_ringbuf_empty(fifo_ringbuf_t* rb)
{
    if (! rb)
        return 1;
    xSemaphoreTake(rb->mutex, portMAX_DELAY);
    int empty = (rb->count == 0);
    xSemaphoreGive(rb->mutex);
    return empty ? 1 : 0;
}

size_t fifo_ringbuf_size(fifo_ringbuf_t* rb)
{
    if (! rb)
        return 0;
    xSemaphoreTake(rb->mutex, portMAX_DELAY);
    size_t sz = rb->count;
    xSemaphoreGive(rb->mutex);
    return sz;
}
