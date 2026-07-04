#pragma once

#include <stdbool.h>
#include <stdint.h>

#define COMPANION_TEXT_LEN 256
#define COMPANION_ID_LEN 80

typedef enum {
    COMPANION_STATUS_DISCONNECTED,
    COMPANION_STATUS_WIFI,
    COMPANION_STATUS_DISCOVERING,
    COMPANION_STATUS_CONNECTING,
    COMPANION_STATUS_CONNECTED,
    COMPANION_STATUS_AUTHENTICATED,
    COMPANION_STATUS_ERROR
} companion_status_t;

typedef struct {
    char state[40];
    char session_id[COMPANION_ID_LEN];
    char model[64];
    char project[96];
    int connected_clients;
} companion_state_t;

typedef struct {
    char event[64];
    char tool_name[64];
    char detail[COMPANION_TEXT_LEN];
    char timestamp[48];
} companion_event_t;

typedef struct {
    char request_id[COMPANION_ID_LEN];
    char tool_name[64];
    char message[COMPANION_TEXT_LEN];
    char command[COMPANION_TEXT_LEN];
    char file_path[COMPANION_TEXT_LEN];
    char timestamp[48];
} companion_permission_t;

typedef struct {
    int duration_s;
    int events_count;
    int files_count;
    int commands_count;
} companion_summary_t;
