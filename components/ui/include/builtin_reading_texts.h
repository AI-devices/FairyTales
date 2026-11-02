#pragma once

#include "builtin_texts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Paragraphs are separated with a blank line ("\n\n"). */
int builtin_reading_text_count(void);
const char *builtin_get_reading_text(void);
const char *builtin_get_reading_text_for_case(builtin_text_case_t c);

#ifdef __cplusplus
}
#endif
