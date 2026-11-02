#pragma once

#include <stddef.h>
#include <stdint.h>

#include "builtin_texts.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    builtin_text_case_t case_id;
    const char *node_name;
    const char *spiffs_path;
    uint32_t raw_size;
    uint16_t width;
    uint16_t height;
    lv_img_cf_t color_format;
} story_asset_desc_t;

extern const story_asset_desc_t k_story_assets[CASE_TXT_COUNT];

const story_asset_desc_t *story_assets_get_desc(builtin_text_case_t case_id);
size_t story_assets_count(void);

#ifdef __cplusplus
}
#endif
