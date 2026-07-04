#pragma once

#include "companion_protocol.h"

typedef void (*companion_action_cb_t)(const char *request_id, const char *action);

void companion_ui_init(companion_action_cb_t action_cb);
void companion_ui_set_status(companion_status_t status, const char *detail);
void companion_ui_render_state(const companion_state_t *state);
void companion_ui_render_event(const companion_event_t *event);
void companion_ui_render_permission(const companion_permission_t *permission);
void companion_ui_render_summary(const companion_summary_t *summary);
