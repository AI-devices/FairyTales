#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ANIMATION_DECODER_KIND_AUTO = 0,
    ANIMATION_DECODER_KIND_GIF,
    ANIMATION_DECODER_KIND_ANIMATED_WEBP,
    ANIMATION_DECODER_KIND_FRAME_SEQUENCE,
    ANIMATION_DECODER_KIND_CFA,
} animation_decoder_kind_t;

typedef struct animation_decoder animation_decoder_t;

#define ANIMATION_DECODER_RENDERED_FRAME_KEYFRAME          (1U << 0)
#define ANIMATION_DECODER_RENDERED_FRAME_CLEAR_BEFORE_DRAW (1U << 1)
#define ANIMATION_DECODER_RENDERED_FRAME_EMPTY             (1U << 2)

typedef struct
{
    uint16_t width;
    uint16_t height;
    bool has_alpha;
} animation_decoder_canvas_info_t;

typedef struct
{
    uint16_t canvas_width;
    uint16_t canvas_height;
    uint16_t frame_x;
    uint16_t frame_y;
    uint16_t frame_width;
    uint16_t frame_height;
    uint16_t delay_cs;
    uint8_t disposal;
    bool transparency;
    uint8_t transparent_index;
    bool local_palette;
    const uint8_t *frame_indices;
    const uint8_t *palette_rgb888;
    uint16_t palette_size;
} animation_decoder_frame_view_t;

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
} animation_decoder_rendered_frame_view_t;

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
} animation_decoder_cfa_frame_profile_t;

esp_err_t animation_decoder_open(const char *uri,
                                 animation_decoder_kind_t kind,
                                 animation_decoder_t **out_decoder);
void animation_decoder_close(animation_decoder_t *decoder);
esp_err_t animation_decoder_rewind(animation_decoder_t *decoder);
int animation_decoder_decode_next(animation_decoder_t *decoder);
uint16_t animation_decoder_frame_delay_cs(const animation_decoder_t *decoder);
uint32_t animation_decoder_frame_delay_ms(const animation_decoder_t *decoder);
esp_err_t animation_decoder_canvas_info(const animation_decoder_t *decoder,
                                        animation_decoder_canvas_info_t *out_info);
esp_err_t animation_decoder_frame_view(const animation_decoder_t *decoder,
                                       animation_decoder_frame_view_t *out_view);
esp_err_t animation_decoder_rendered_frame_view(const animation_decoder_t *decoder,
                                                animation_decoder_rendered_frame_view_t *out_view);
bool animation_decoder_has_alpha(const animation_decoder_t *decoder);
bool animation_decoder_frame_ready(const animation_decoder_t *decoder);
size_t animation_decoder_file_size(const animation_decoder_t *decoder);
const char *animation_decoder_uri(const animation_decoder_t *decoder);
animation_decoder_kind_t animation_decoder_kind(const animation_decoder_t *decoder);
bool animation_decoder_is_preloaded(const animation_decoder_t *decoder);
esp_err_t animation_decoder_cfa_last_profile(const animation_decoder_t *decoder,
                                             animation_decoder_cfa_frame_profile_t *out_profile);

#ifdef __cplusplus
}
#endif
