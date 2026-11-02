#ifndef _FIFO_RINGBUF_H_
#define _FIFO_RINGBUF_H_

#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fifo_ringbuf_t fifo_ringbuf_t;

/**
 * @brief  Create fifo ring buffer
 *
 * @param[in]  len        Number of items in buffer
 * @param[in]  item_size  Size of item in bytes
 *
 * @return
 *       - NULL    Failed to create
 *       - Others  fifo ringbuf instance
 */
fifo_ringbuf_t* fifo_ringbuf_init(size_t len, size_t item_size);
/**
 * @brief  Destory fifo ring buffer
 *
 * @param[in]  rb  fifo ringbuf instance
 */
void fifo_ringbuf_release(fifo_ringbuf_t* rb);
/**
 * @brief  Write to ringbuf
 *
 * @param[in]  rb    fifo ringbuf instance
 * @param[in]  data  Pointer to data
 * @param[in]  size  Data size in bytes
 * @param[in]  timeout  Wait for available space
 *
 * @return
 *       - >=0   Bytes wrote
 */
size_t fifo_ringbuf_write(fifo_ringbuf_t* rb, const void* data, size_t size, size_t timeout);
/**
 * @brief  Read from ringbuf
 *
 * @param[in]   rb       fifo ringbuf instance
 * @param[out]  data     Pointer to pre-allocated data buffer
 * @param[in]   size     Max bytes to read
 * @param[in]   timeout  Wait timeout in ms
 *
 * @return
 *       - >=0  Bytes read
 */
size_t fifo_ringbuf_read(fifo_ringbuf_t* rb, void* data, size_t max_len, size_t timeout);
/**
 * @brief  Reset ringbuf state
 *
 * @param[in]  rb  fifo ringbuf instance
 *
 * @return
 *       - -1  Fail
 *       - 0   Success
 */
int fifo_ringbuf_reset(fifo_ringbuf_t* rb);
/**
 * @brief  Check if ringbuf is empty
 *
 * @param[in]  rb  fifo ringbuf instance
 *
 * @return
 *       - 0  Not empty
 *       - 1   Empty
 */
int fifo_ringbuf_empty(fifo_ringbuf_t* rb);

size_t fifo_ringbuf_size(fifo_ringbuf_t* rb);

#ifdef __cplusplus
}
#endif

#endif // _FIFO_RINGBUF_H_
