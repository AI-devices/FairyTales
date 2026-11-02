#pragma once

#if defined __has_include
#if __has_include("lvgl.h")
#include "lvgl.h"
#elif __has_include("lvgl/lvgl.h")
#include "lvgl/lvgl.h"
#else
#include "lvgl.h"
#endif
#else
#include "lvgl.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void ui_TitleScreen_screen_init(void);
void ui_TitleScreen_screen_destroy(void);
void ui_TitleScreen_start_intro_animation(void);

extern lv_obj_t *ui_TitleScreen;

#ifdef __cplusplus
}
#endif
