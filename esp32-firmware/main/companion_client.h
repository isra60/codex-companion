#pragma once

#include "esp_err.h"

esp_err_t companion_client_start(void);
void companion_client_send_action(const char *request_id, const char *action);
