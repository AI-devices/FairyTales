// SPDX-License-Identifier: BSD-2-Clause
/*
 * Minimal LZ4 block decoder with LZ4_decompress_safe-compatible semantics.
 * This intentionally supports only raw LZ4 blocks, not the LZ4 frame format.
 */

#include "lz4_block.h"

#include <string.h>

static int lz4_read_length(const uint8_t **cursor, const uint8_t *end, size_t *length)
{
    if (!cursor || !*cursor || !length) {
        return -1;
    }

    while (*cursor < end) {
        const uint8_t value = **cursor;
        ++(*cursor);
        *length += value;
        if (value != 255U) {
            return 0;
        }
    }

    return -1;
}

int lz4_block_decompress_safe(const uint8_t *src,
                              size_t src_size,
                              uint8_t *dst,
                              size_t dst_capacity)
{
    const uint8_t *ip = src;
    const uint8_t *iend = src ? src + src_size : NULL;
    uint8_t *op = dst;
    uint8_t *oend = dst ? dst + dst_capacity : NULL;

    if ((!src && src_size != 0U) || !dst) {
        return -1;
    }

    while (ip < iend) {
        const uint8_t token = *ip++;
        size_t literal_len = (size_t)(token >> 4);
        size_t match_len = (size_t)(token & 0x0F);
        uint16_t offset = 0;
        uint8_t *match = NULL;

        if (literal_len == 15U && lz4_read_length(&ip, iend, &literal_len) != 0) {
            return -1;
        }
        if ((size_t)(iend - ip) < literal_len || (size_t)(oend - op) < literal_len) {
            return -1;
        }
        if (literal_len > 0U) {
            memcpy(op, ip, literal_len);
            ip += literal_len;
            op += literal_len;
        }

        if (ip >= iend) {
            break;
        }
        if ((size_t)(iend - ip) < 2U) {
            return -1;
        }

        offset = (uint16_t)ip[0] | ((uint16_t)ip[1] << 8);
        ip += 2;
        if (offset == 0U || (size_t)offset > (size_t)(op - dst)) {
            return -1;
        }

        match_len += 4U;
        if ((token & 0x0F) == 15U && lz4_read_length(&ip, iend, &match_len) != 0) {
            return -1;
        }
        if ((size_t)(oend - op) < match_len) {
            return -1;
        }

        match = op - offset;
        while (match_len-- > 0U) {
            *op++ = *match++;
        }
    }

    return (int)(op - dst);
}
