#include "animation_decoder.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "animation_cfa_decoder.h"
#include "gifdec.h"
#include "media_storage.h"

static const char *TAG = "anim_decoder";

typedef struct
{
    gd_GIF *gif;
    bool frame_ready;
} animation_decoder_gif_t;

typedef struct
{
    animation_cfa_decoder_t *cfa;
} animation_decoder_cfa_t;

struct animation_decoder
{
    char *uri;
    animation_decoder_kind_t kind;
    union
    {
        animation_decoder_gif_t gif;
        animation_decoder_cfa_t cfa;
    } impl;
};

static animation_decoder_kind_t animation_decoder_detect_kind(const char *uri)
{
    const char *ext = NULL;

    if (!uri) {
        return ANIMATION_DECODER_KIND_AUTO;
    }

    ext = strrchr(uri, '.');
    if (!ext || ext[1] == '\0') {
        return ANIMATION_DECODER_KIND_AUTO;
    }

    ++ext;
    if (strcasecmp(ext, "gif") == 0) {
        return ANIMATION_DECODER_KIND_GIF;
    }
    if (strcasecmp(ext, "webp") == 0) {
        return ANIMATION_DECODER_KIND_ANIMATED_WEBP;
    }
    if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
        return ANIMATION_DECODER_KIND_FRAME_SEQUENCE;
    }
    if (strcasecmp(ext, "cfa") == 0) {
        return ANIMATION_DECODER_KIND_CFA;
    }

    return ANIMATION_DECODER_KIND_AUTO;
}

esp_err_t animation_decoder_open(const char *uri,
                                 animation_decoder_kind_t kind,
                                 animation_decoder_t **out_decoder)
{
    animation_decoder_t *decoder = NULL;
    char path[256] = {0};

    if (!uri || !out_decoder) {
        return ESP_ERR_INVALID_ARG;
    }

    if (kind == ANIMATION_DECODER_KIND_AUTO) {
        kind = animation_decoder_detect_kind(uri);
    }

    if (kind != ANIMATION_DECODER_KIND_GIF && kind != ANIMATION_DECODER_KIND_CFA) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    decoder = calloc(1, sizeof(*decoder));
    if (!decoder) {
        return ESP_ERR_NO_MEM;
    }

    decoder->uri = strdup(uri);
    if (!decoder->uri) {
        free(decoder);
        return ESP_ERR_NO_MEM;
    }

    decoder->kind = kind;
    if (kind == ANIMATION_DECODER_KIND_CFA) {
        esp_err_t err = animation_cfa_decoder_open(uri, &decoder->impl.cfa.cfa);

        if (err != ESP_OK) {
            animation_decoder_close(decoder);
            return err;
        }
    } else {
        esp_err_t err = media_storage_ensure_ready_for_uri(uri);

        if (err != ESP_OK) {
            animation_decoder_close(decoder);
            return err;
        }
        err = media_storage_resolve_path(uri, path, sizeof(path));
        if (err != ESP_OK) {
            animation_decoder_close(decoder);
            return err;
        }

        decoder->impl.gif.gif = gd_open_gif(path);
        if (!decoder->impl.gif.gif) {
            ESP_LOGE(TAG, "gd_open_gif failed uri=%s path=%s", uri, path);
            animation_decoder_close(decoder);
            return ESP_FAIL;
        }

        decoder->impl.gif.frame_ready = false;
    }

    *out_decoder = decoder;
    return ESP_OK;
}

void animation_decoder_close(animation_decoder_t *decoder)
{
    if (!decoder) {
        return;
    }

    if (decoder->kind == ANIMATION_DECODER_KIND_GIF && decoder->impl.gif.gif) {
        gd_close_gif(decoder->impl.gif.gif);
    } else if (decoder->kind == ANIMATION_DECODER_KIND_CFA && decoder->impl.cfa.cfa) {
        animation_cfa_decoder_close(decoder->impl.cfa.cfa);
    }

    free(decoder->uri);
    free(decoder);
}

esp_err_t animation_decoder_rewind(animation_decoder_t *decoder)
{
    if (!decoder) {
        return ESP_ERR_INVALID_ARG;
    }

    if (decoder->kind == ANIMATION_DECODER_KIND_CFA && decoder->impl.cfa.cfa) {
        return animation_cfa_decoder_rewind(decoder->impl.cfa.cfa);
    }

    if (decoder->kind != ANIMATION_DECODER_KIND_GIF || !decoder->impl.gif.gif) {
        return ESP_ERR_INVALID_STATE;
    }

    gd_rewind(decoder->impl.gif.gif);
    decoder->impl.gif.frame_ready = false;
    return ESP_OK;
}

int animation_decoder_decode_next(animation_decoder_t *decoder)
{
    int result = 0;

    if (!decoder) {
        return -1;
    }

    if (decoder->kind == ANIMATION_DECODER_KIND_CFA && decoder->impl.cfa.cfa) {
        return animation_cfa_decoder_decode_next(decoder->impl.cfa.cfa);
    }

    if (decoder->kind != ANIMATION_DECODER_KIND_GIF || !decoder->impl.gif.gif) {
        return -1;
    }

    result = gd_get_frame(decoder->impl.gif.gif);
    decoder->impl.gif.frame_ready = result > 0;
    return result;
}

uint16_t animation_decoder_frame_delay_cs(const animation_decoder_t *decoder)
{
    if (!decoder) {
        return 0U;
    }

    if (decoder->kind == ANIMATION_DECODER_KIND_CFA && decoder->impl.cfa.cfa) {
        uint32_t delay_ms = animation_cfa_decoder_frame_delay_ms(decoder->impl.cfa.cfa);

        return (uint16_t)(delay_ms / 10U);
    }

    if (decoder->kind != ANIMATION_DECODER_KIND_GIF || !decoder->impl.gif.gif) {
        return 0U;
    }

    return decoder->impl.gif.gif->gce.delay;
}

uint32_t animation_decoder_frame_delay_ms(const animation_decoder_t *decoder)
{
    uint16_t delay_cs = 0;

    if (decoder && decoder->kind == ANIMATION_DECODER_KIND_CFA && decoder->impl.cfa.cfa) {
        return animation_cfa_decoder_frame_delay_ms(decoder->impl.cfa.cfa);
    }

    delay_cs = animation_decoder_frame_delay_cs(decoder);
    if (delay_cs == 0U) {
        return 30U;
    }

    if (delay_cs < 3U) {
        delay_cs = 3U;
    }

    return (uint32_t)delay_cs * 10U;
}

esp_err_t animation_decoder_canvas_info(const animation_decoder_t *decoder,
                                        animation_decoder_canvas_info_t *out_info)
{
    gd_GIF *gif = NULL;

    if (!decoder || !out_info) {
        return ESP_ERR_INVALID_ARG;
    }

    if (decoder->kind == ANIMATION_DECODER_KIND_CFA && decoder->impl.cfa.cfa) {
        uint16_t width = 0;
        uint16_t height = 0;
        esp_err_t err = animation_cfa_decoder_canvas_info(decoder->impl.cfa.cfa, &width, &height);

        if (err != ESP_OK) {
            return err;
        }
        out_info->width = width;
        out_info->height = height;
        out_info->has_alpha = true;
        return ESP_OK;
    }

    if (decoder->kind != ANIMATION_DECODER_KIND_GIF || !decoder->impl.gif.gif) {
        return ESP_ERR_INVALID_STATE;
    }

    gif = decoder->impl.gif.gif;
    out_info->width = gif->width;
    out_info->height = gif->height;
    out_info->has_alpha = true;
    return ESP_OK;
}

esp_err_t animation_decoder_rendered_frame_view(const animation_decoder_t *decoder,
                                                animation_decoder_rendered_frame_view_t *out_view)
{
    animation_cfa_frame_view_t cfa_view = {0};
    esp_err_t err = ESP_OK;

    if (!decoder || !out_view) {
        return ESP_ERR_INVALID_ARG;
    }

    if (decoder->kind != ANIMATION_DECODER_KIND_CFA || !decoder->impl.cfa.cfa) {
        return ESP_ERR_INVALID_STATE;
    }

    err = animation_cfa_decoder_frame_view(decoder->impl.cfa.cfa, &cfa_view);
    if (err != ESP_OK) {
        return err;
    }

    memset(out_view, 0, sizeof(*out_view));
    out_view->canvas_width = cfa_view.canvas_width;
    out_view->canvas_height = cfa_view.canvas_height;
    out_view->frame_x = cfa_view.frame_x;
    out_view->frame_y = cfa_view.frame_y;
    out_view->frame_width = cfa_view.frame_width;
    out_view->frame_height = cfa_view.frame_height;
    out_view->delay_ms = cfa_view.delay_ms;
    out_view->flags = cfa_view.flags;
    out_view->rgb565 = cfa_view.rgb565;
    out_view->alpha = cfa_view.alpha;
    out_view->pixel_count = cfa_view.pixel_count;
    return ESP_OK;
}

esp_err_t animation_decoder_frame_view(const animation_decoder_t *decoder,
                                       animation_decoder_frame_view_t *out_view)
{
    gd_GIF *gif = NULL;

    if (!decoder || !out_view) {
        return ESP_ERR_INVALID_ARG;
    }

    if (decoder->kind != ANIMATION_DECODER_KIND_GIF || !decoder->impl.gif.gif || !decoder->impl.gif.frame_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    gif = decoder->impl.gif.gif;
    memset(out_view, 0, sizeof(*out_view));
    out_view->canvas_width = gif->width;
    out_view->canvas_height = gif->height;
    out_view->frame_x = gif->fx;
    out_view->frame_y = gif->fy;
    out_view->frame_width = gif->fw;
    out_view->frame_height = gif->fh;
    out_view->delay_cs = gif->gce.delay;
    out_view->disposal = gif->gce.disposal;
    out_view->transparency = gif->gce.transparency != 0;
    out_view->transparent_index = gif->gce.tindex;
    out_view->local_palette = gif->palette == &gif->lct;
    out_view->frame_indices = gif->frame;
    out_view->palette_rgb888 = gif->palette ? gif->palette->colors : NULL;
    out_view->palette_size = gif->palette ? (uint16_t)gif->palette->size : 0U;
    return ESP_OK;
}

bool animation_decoder_has_alpha(const animation_decoder_t *decoder)
{
    animation_decoder_canvas_info_t info = {0};

    return animation_decoder_canvas_info(decoder, &info) == ESP_OK && info.has_alpha;
}

bool animation_decoder_frame_ready(const animation_decoder_t *decoder)
{
    if (!decoder) {
        return false;
    }
    if (decoder->kind == ANIMATION_DECODER_KIND_CFA && decoder->impl.cfa.cfa) {
        return animation_cfa_decoder_frame_ready(decoder->impl.cfa.cfa);
    }

    return decoder->kind == ANIMATION_DECODER_KIND_GIF && decoder->impl.gif.frame_ready;
}

size_t animation_decoder_file_size(const animation_decoder_t *decoder)
{
    if (!decoder) {
        return 0U;
    }

    if (decoder->kind == ANIMATION_DECODER_KIND_CFA && decoder->impl.cfa.cfa) {
        return animation_cfa_decoder_file_size(decoder->impl.cfa.cfa);
    }

    if (decoder->kind != ANIMATION_DECODER_KIND_GIF || !decoder->impl.gif.gif) {
        return 0U;
    }

    return decoder->impl.gif.gif->size;
}

const char *animation_decoder_uri(const animation_decoder_t *decoder)
{
    return decoder ? decoder->uri : NULL;
}

animation_decoder_kind_t animation_decoder_kind(const animation_decoder_t *decoder)
{
    return decoder ? decoder->kind : ANIMATION_DECODER_KIND_AUTO;
}

bool animation_decoder_is_preloaded(const animation_decoder_t *decoder)
{
    if (!decoder) {
        return false;
    }

    if (decoder->kind == ANIMATION_DECODER_KIND_CFA && decoder->impl.cfa.cfa) {
        return animation_cfa_decoder_is_preloaded(decoder->impl.cfa.cfa);
    }

    return decoder->kind == ANIMATION_DECODER_KIND_GIF && decoder->impl.gif.gif != NULL;
}

esp_err_t animation_decoder_cfa_last_profile(const animation_decoder_t *decoder,
                                             animation_decoder_cfa_frame_profile_t *out_profile)
{
    animation_cfa_frame_profile_t cfa_profile = {0};
    esp_err_t err = ESP_OK;

    if (!decoder || !out_profile) {
        return ESP_ERR_INVALID_ARG;
    }
    if (decoder->kind != ANIMATION_DECODER_KIND_CFA || !decoder->impl.cfa.cfa) {
        return ESP_ERR_INVALID_STATE;
    }

    err = animation_cfa_decoder_last_profile(decoder->impl.cfa.cfa, &cfa_profile);
    if (err != ESP_OK) {
        return err;
    }

    memset(out_profile, 0, sizeof(*out_profile));
    out_profile->frame_index = cfa_profile.frame_index;
    out_profile->frame_width = cfa_profile.frame_width;
    out_profile->frame_height = cfa_profile.frame_height;
    out_profile->codec = cfa_profile.codec;
    out_profile->preloaded = cfa_profile.preloaded;
    out_profile->rect_pixels = cfa_profile.rect_pixels;
    out_profile->raw_size = cfa_profile.raw_size;
    out_profile->payload_size = cfa_profile.payload_size;
    out_profile->read_us = cfa_profile.read_us;
    out_profile->crc_us = cfa_profile.crc_us;
    out_profile->decompress_us = cfa_profile.decompress_us;
    out_profile->total_us = cfa_profile.total_us;
    return ESP_OK;
}
