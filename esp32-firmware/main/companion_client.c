#include "companion_client.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "companion_protocol.h"
#include "companion_ui.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "mdns.h"
#include "sdkconfig.h"

static const char *TAG = "companion";
static esp_websocket_client_handle_t s_client;

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static esp_err_t discover_uri(char *uri, size_t uri_len);
static void handle_text_message(const char *data, int len);
static void send_auth(void);
static void json_copy(cJSON *object, const char *name, char *dest, size_t len);
static int json_array_size(cJSON *object, const char *name);
static void project_from_cwd(const char *cwd, char *dest, size_t len);

esp_err_t companion_client_start(void)
{
    char uri[160];
    ESP_RETURN_ON_ERROR(discover_uri(uri, sizeof(uri)), TAG, "discovery failed");

    companion_ui_set_status(COMPANION_STATUS_CONNECTING, uri);
    esp_websocket_client_config_t websocket_cfg = {
        .uri = uri,
        .reconnect_timeout_ms = 3000,
        .network_timeout_ms = 10000,
    };
    s_client = esp_websocket_client_init(&websocket_cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_ERR_NO_MEM, TAG, "websocket init failed");
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL), TAG, "websocket event register failed");
    return esp_websocket_client_start(s_client);
}

void companion_client_send_action(const char *request_id, const char *action)
{
    if (!s_client || !esp_websocket_client_is_connected(s_client)) {
        companion_ui_set_status(COMPANION_STATUS_ERROR, "WebSocket not connected");
        return;
    }

    char payload[192];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"action\",\"request_id\":\"%s\",\"action\":\"%s\"}",
             request_id,
             action);
    esp_websocket_client_send_text(s_client, payload, strlen(payload), portMAX_DELAY);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        companion_ui_set_status(COMPANION_STATUS_CONNECTED, "Authenticating");
        send_auth();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        companion_ui_set_status(COMPANION_STATUS_DISCONNECTED, "WebSocket disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x1 && data->data_len > 0) {
            handle_text_message(data->data_ptr, data->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        companion_ui_set_status(COMPANION_STATUS_ERROR, "WebSocket error");
        break;
    default:
        break;
    }
}

static esp_err_t discover_uri(char *uri, size_t uri_len)
{
    companion_ui_set_status(COMPANION_STATUS_DISCOVERING, "mDNS _codex-companion._tcp");
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mdns_init failed");
    mdns_hostname_set("codex-companion-esp32");
    mdns_instance_name_set("Codex Companion ESP32");

    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(
        CONFIG_COMPANION_MDNS_SERVICE,
        CONFIG_COMPANION_MDNS_PROTO,
        CONFIG_COMPANION_MDNS_QUERY_TIMEOUT_MS,
        1,
        &results);

    if (err == ESP_OK && results) {
        const char *host = results->hostname ? results->hostname : results->instance_name;
        if (host && results->port > 0) {
            snprintf(uri, uri_len, "ws://%s.local:%u", host, results->port);
            ESP_LOGI(TAG, "Discovered companion: %s", uri);
            mdns_query_results_free(results);
            return ESP_OK;
        }
        mdns_query_results_free(results);
    }

    if (strlen(CONFIG_COMPANION_FALLBACK_HOST) > 0) {
        snprintf(uri, uri_len, "ws://%s:%d", CONFIG_COMPANION_FALLBACK_HOST, CONFIG_COMPANION_FALLBACK_PORT);
        ESP_LOGW(TAG, "mDNS failed, using fallback: %s", uri);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "No mDNS result for %s.%s", CONFIG_COMPANION_MDNS_SERVICE, CONFIG_COMPANION_MDNS_PROTO);
    companion_ui_set_status(COMPANION_STATUS_ERROR, "mDNS companion not found");
    return ESP_ERR_NOT_FOUND;
}

static void send_auth(void)
{
    if (strlen(CONFIG_COMPANION_AUTH_TOKEN) == 0) {
        companion_ui_set_status(COMPANION_STATUS_ERROR, "Auth token empty");
        return;
    }

    char payload[192];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"auth\",\"token\":\"%s\"}",
             CONFIG_COMPANION_AUTH_TOKEN);
    esp_websocket_client_send_text(s_client, payload, strlen(payload), portMAX_DELAY);
}

static void handle_text_message(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON from companion");
        return;
    }

    char type[40] = {0};
    json_copy(root, "type", type, sizeof(type));

    if (strcmp(type, "auth") == 0) {
        cJSON *ok = cJSON_GetObjectItem(root, "ok");
        if (cJSON_IsTrue(ok)) {
            companion_ui_set_status(COMPANION_STATUS_AUTHENTICATED, "Waiting for Codex");
        } else {
            companion_ui_set_status(COMPANION_STATUS_ERROR, "Auth rejected");
        }
    } else if (strcmp(type, "state") == 0) {
        companion_state_t state = {0};
        json_copy(root, "state", state.state, sizeof(state.state));
        cJSON *session = cJSON_GetObjectItem(root, "session");
        if (cJSON_IsObject(session)) {
            char cwd[COMPANION_TEXT_LEN] = {0};
            json_copy(session, "session_id", state.session_id, sizeof(state.session_id));
            json_copy(session, "model", state.model, sizeof(state.model));
            json_copy(session, "cwd", cwd, sizeof(cwd));
            project_from_cwd(cwd, state.project, sizeof(state.project));
        }
        cJSON *clients = cJSON_GetObjectItem(root, "connected_clients");
        state.connected_clients = cJSON_IsNumber(clients) ? clients->valueint : 0;
        companion_ui_render_state(&state);
    } else if (strcmp(type, "event") == 0) {
        companion_event_t event = {0};
        json_copy(root, "event", event.event, sizeof(event.event));
        json_copy(root, "tool_name", event.tool_name, sizeof(event.tool_name));
        json_copy(root, "detail", event.detail, sizeof(event.detail));
        json_copy(root, "timestamp", event.timestamp, sizeof(event.timestamp));
        companion_ui_render_event(&event);
    } else if (strcmp(type, "permission") == 0) {
        companion_permission_t permission = {0};
        json_copy(root, "request_id", permission.request_id, sizeof(permission.request_id));
        json_copy(root, "tool_name", permission.tool_name, sizeof(permission.tool_name));
        json_copy(root, "message", permission.message, sizeof(permission.message));
        json_copy(root, "command", permission.command, sizeof(permission.command));
        json_copy(root, "file_path", permission.file_path, sizeof(permission.file_path));
        json_copy(root, "timestamp", permission.timestamp, sizeof(permission.timestamp));
        companion_ui_render_permission(&permission);
    } else if (strcmp(type, "summary") == 0) {
        companion_summary_t summary = {0};
        cJSON *duration = cJSON_GetObjectItem(root, "duration_s");
        cJSON *events = cJSON_GetObjectItem(root, "events_count");
        summary.duration_s = cJSON_IsNumber(duration) ? duration->valueint : 0;
        summary.events_count = cJSON_IsNumber(events) ? events->valueint : 0;
        summary.files_count = json_array_size(root, "files_modified");
        summary.commands_count = json_array_size(root, "commands_run");
        companion_ui_render_summary(&summary);
    } else if (strcmp(type, "pong") == 0) {
        ESP_LOGD(TAG, "pong");
    }

    cJSON_Delete(root);
}

static void json_copy(cJSON *object, const char *name, char *dest, size_t len)
{
    cJSON *item = cJSON_GetObjectItem(object, name);
    if (cJSON_IsString(item) && item->valuestring) {
        strlcpy(dest, item->valuestring, len);
    }
}

static int json_array_size(cJSON *object, const char *name)
{
    cJSON *item = cJSON_GetObjectItem(object, name);
    return cJSON_IsArray(item) ? cJSON_GetArraySize(item) : 0;
}

static void project_from_cwd(const char *cwd, char *dest, size_t len)
{
    if (!cwd || !cwd[0]) {
        strlcpy(dest, "-", len);
        return;
    }
    const char *last_slash = strrchr(cwd, '/');
    const char *last_backslash = strrchr(cwd, '\\');
    const char *last = last_slash > last_backslash ? last_slash : last_backslash;
    strlcpy(dest, last ? last + 1 : cwd, len);
}
