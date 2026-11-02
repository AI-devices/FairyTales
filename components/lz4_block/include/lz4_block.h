#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int lz4_block_decompress_safe(const uint8_t *src,
                              size_t src_size,
                              uint8_t *dst,
                              size_t dst_capacity);

#ifdef __cplusplus
}
#endif
