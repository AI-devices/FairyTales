#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANIMATION_CFA_FRAME_KEYFRAME          (1U << 0)
#define ANIMATION_CFA_FRAME_CLEAR_BEFORE_DRAW (1U << 1)
#define ANIMATION_CFA_FRAME_EMPTY             (1U << 2)

typedef struct animation_cfa_decoder animation_cfa_decoder_t;

typedef struct
{
    uint16_t canvas_width;
    uint16_t canvas_height;
    uint16_t frame_x;
    uint16_t frame_y;
    uint16_t frame_width;
    uint16_t frame_height;
    uint16_t delay_ms;
    uint16_t flags;
    const uint8_t *rgb565;
    const uint8_t *alpha;
    size_t pixel_count;
} animation_cfa_frame_view_t;

typedef struct
{
    uint32_t frame_index;
    uint16_t frame_width;
    uint16_t frame_height;
    uint8_t codec;
    bool preloaded;
    size_t rect_pixels;
    size_t raw_size;
    size_t payload_size;
    uint64_t read_us;
    uint64_t crc_us;
    uint64_t decompress_us;
    uint64_t total_us;
} animation_cfa_frame_profile_t;

esp_err_t animation_cfa_decoder_open(const char *uri, animation_cfa_decoder_t **out_decoder);
void animation_cfa_decoder_close(animation_cfa_decoder_t *decoder);
esp_err_t animation_cfa_decoder_rewind(animation_cfa_decoder_t *decoder);
int animation_cfa_decoder_decode_next(animation_cfa_decoder_t *decoder);
esp_err_t animation_cfa_decoder_canvas_info(const animation_cfa_decoder_t *decoder,
                                            uint16_t *out_width,
                                            uint16_t *out_height);
esp_err_t animation_cfa_decoder_frame_view(const animation_cfa_decoder_t *decoder,
                                           animation_cfa_frame_view_t *out_view);
bool animation_cfa_decoder_frame_ready(const animation_cfa_decoder_t *decoder);
uint32_t animation_cfa_decoder_frame_delay_ms(const animation_cfa_decoder_t *decoder);
size_t animation_cfa_decoder_file_size(const animation_cfa_decoder_t *decoder);
bool animation_cfa_decoder_is_preloaded(const animation_cfa_decoder_t *decoder);
esp_err_t animation_cfa_decoder_last_profile(const animation_cfa_decoder_t *decoder,
                                             animation_cfa_frame_profile_t *out_profile);

#ifdef __cplusplus
}
#endif
