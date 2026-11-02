#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    void (*continue_requested)(void *user_ctx);
    void *user_ctx;
} ui_provision_screen_callbacks_t;

void ui_provision_screen_init(void);
lv_obj_t *ui_provision_screen_root(void);
void ui_provision_screen_destroy(void);
void ui_provision_screen_register_callbacks(const ui_provision_screen_callbacks_t *callbacks);

#ifdef __cplusplus
}
#endif
