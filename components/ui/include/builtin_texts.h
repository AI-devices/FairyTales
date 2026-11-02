#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CASE_TXT_01 = 0,
    CASE_TXT_02,
    CASE_TXT_03,
    CASE_TXT_04,
    CASE_TXT_05,
    CASE_TXT_06,
    CASE_TXT_07,
    CASE_TXT_08,
    CASE_TXT_09,
    CASE_TXT_10,
    CASE_TXT_11,
    CASE_TXT_12,
    CASE_TXT_13,
    CASE_TXT_14,
    CASE_TXT_15,
    CASE_TXT_16,
    CASE_TXT_17,
    CASE_TXT_18,
    CASE_TXT_19,
    CASE_TXT_20,
    CASE_TXT_21,
    CASE_TXT_22,
    CASE_TXT_23,
    CASE_TXT_24,
    CASE_TXT_25,
    CASE_TXT_26,
    CASE_TXT_27,
    CASE_TXT_28,
    CASE_TXT_29,
    CASE_TXT_30,
    CASE_TXT_31,
    CASE_TXT_COUNT /* maximum slots available in UI wiring */
} builtin_text_case_t;

/* Returns the number of texts actually provided by the active scenario.
 * May be <= CASE_TXT_COUNT.
 */
int builtin_text_count(void);

const char* builtin_get_intro_text(void);
const char *builtin_get_outro_text(void);

const char* get_builtin_text(void);
void builtin_text_next(void);
void builtin_text_set(builtin_text_case_t c);
builtin_text_case_t builtin_text_get(void);

#ifdef __cplusplus
}
#endif
