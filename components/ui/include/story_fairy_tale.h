#pragma once
#include <stdbool.h>
#include "builtin_texts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *question;    
    const char *choice1;     
    const char *choice2;     
    bool is_final;           
    builtin_text_case_t next1; 
    builtin_text_case_t next2; 
} story_node_t;

const story_node_t* story_get_node(builtin_text_case_t c);

#define STORY_START_CASE  CASE_TXT_01

#ifdef __cplusplus
}
#endif
