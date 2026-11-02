#include <stdio.h>
#include <string.h>

#include "settings_store.h"

#include "user_name_store.h"

static char s_user_name[USER_NAME_MAX_LEN + 1] = {0};
static int  s_loaded = 0;  

static void user_name_try_load(void)
{
    if (s_loaded) {
        return;
    }

    bool found = false;
    if (settings_store_get_user_name(s_user_name, sizeof(s_user_name), &found) != ESP_OK || !found) {
        s_user_name[0] = '\0';
        s_loaded = 1;
        return;
    }

    s_loaded = 1;
}

const char *user_name_get(void)
{
    user_name_try_load();
    if (s_user_name[0] == '\0') {
        return NULL;
    }
    return s_user_name;
}

void user_name_set(const char *name)
{
    user_name_try_load();  

    if (!name) {
        return;
    }

    size_t len = strnlen(name, USER_NAME_MAX_LEN);
    memcpy(s_user_name, name, len);
    s_user_name[len] = '\0';

    (void)settings_store_set_user_name(s_user_name);
}

void user_name_reset_cache(void)
{
    s_user_name[0] = '\0';
    s_loaded = 0;
}
