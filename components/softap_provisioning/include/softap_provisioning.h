#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t softap_provisioning_init(void);
esp_err_t softap_provisioning_start(void);
void softap_provisioning_stop(void);

bool softap_provisioning_is_running(void);
bool softap_provisioning_is_stopping(void);
bool softap_provisioning_has_saved_name(void);

const char *softap_provisioning_get_ap_ssid(void);
const char *softap_provisioning_get_ap_password(void);
const char *softap_provisioning_get_ap_url(void);

#ifdef __cplusplus
}
#endif
