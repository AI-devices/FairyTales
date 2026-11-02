#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t* _ui_load_binary_direct(const char* fname_S, uint32_t size);

#define UI_LOAD_IMAGE _ui_load_binary_direct

#ifdef __cplusplus
}
#endif
