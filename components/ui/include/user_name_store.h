#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define USER_NAME_MAX_LEN 63

const char *user_name_get(void);

void user_name_set(const char *name);
void user_name_reset_cache(void);

#ifdef __cplusplus
}
#endif
