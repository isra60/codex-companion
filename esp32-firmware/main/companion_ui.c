#include "companion_ui.h"

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"

static const char *TAG = "ui";

static companion_action_cb_t s_action_cb;
static char s_current_request_id[COMPANION_ID_LEN];

static lv_obj_t *s_status_label;
static lv_obj_t *s_detail_label;
static lv_obj_t *s_session_label;
static lv_obj_t *s_event_label;
static lv_obj_t *s_permission_panel;
static lv_obj_t *s_permission_title;
static lv_obj_t *s_permission_detail;
static lv_obj_t *s_summary_label;

static void allow_event_cb(lv_event_t *event);
static void deny_event_cb(lv_event_t *event);

static void rounder_event_cb(lv_event_t *event)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(event);
    if (!area) {
        return;
    }

    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static bool ui_lock(int32_t timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

static void ui_unlock(void)
{
    esp_lv_adapter_unlock();
}

static lv_display_t *companion_display_start(void)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {
            .swap_xy = 1,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };

    if (esp_lv_adapter_init(&cfg.lv_adapter_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_lv_adapter_init failed");
        return NULL;
    }

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t panel_io = NULL;
    const bsp_display_config_t display_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * 20 * BSP_LCD_BITS_PER_PIXEL / 8,
    };
    if (bsp_display_new(&display_cfg, &panel, &panel_io) != ESP_OK) {
        ESP_LOGE(TAG, "bsp_display_new failed");
        return NULL;
    }

    esp_lv_adapter_display_config_t adapter_display_cfg = {
        .panel = panel,
        .panel_io = panel_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = cfg.rotation,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            .buffer_height = 20,
            .use_psram = false,
            .enable_ppa_accel = false,
            .require_double_buffer = false,
        },
        .tear_avoid_mode = cfg.tear_avoid_mode,
    };

    lv_display_t *display = esp_lv_adapter_register_display(&adapter_display_cfg);
    if (!display) {
        ESP_LOGE(TAG, "esp_lv_adapter_register_display failed");
        return NULL;
    }
    lv_display_add_event_cb(display, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    esp_lcd_touch_handle_t touch = NULL;
    if (bsp_touch_new(&cfg, &touch) == ESP_OK) {
        const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch);
        if (!esp_lv_adapter_register_touch(&touch_cfg)) {
            ESP_LOGW(TAG, "touch registration failed");
        }
    } else {
        ESP_LOGW(TAG, "touch init failed");
    }

    if (bsp_display_brightness_init() != ESP_OK) {
        ESP_LOGW(TAG, "brightness init failed");
    }

    if (esp_lv_adapter_start() != ESP_OK) {
        ESP_LOGE(TAG, "esp_lv_adapter_start failed");
        return NULL;
    }
    return display;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_color_t color, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 190, 72);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_darken(color, 210), 0);
    lv_obj_set_style_border_color(button, color, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_center(label);
    return button;
}

void companion_ui_init(companion_action_cb_t action_cb)
{
    s_action_cb = action_cb;
    lv_display_t *display = companion_display_start();
    if (!display) {
        ESP_LOGE(TAG, "bsp_display_start failed");
        return;
    }

    if (!ui_lock(-1)) {
        ESP_LOGE(TAG, "display lock failed during init");
        return;
    }
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x050505), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xf5f5f5), 0);

    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 480);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 14, 0);
    lv_obj_set_style_pad_row(root, 10, 0);

    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, 452, 38);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(header, "Codex Companion", &lv_font_montserrat_18, lv_color_hex(0xf5f5f5));
    make_label(header, "ESP32", &lv_font_montserrat_14, lv_color_hex(0x4488ff));

    s_status_label = make_label(root, "Booting", &lv_font_montserrat_24, lv_color_hex(0xffb800));
    lv_obj_set_width(s_status_label, 452);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);

    s_detail_label = make_label(root, "Starting display", &lv_font_montserrat_14, lv_color_hex(0xa0a0a0));
    lv_obj_set_width(s_detail_label, 452);
    lv_obj_set_style_text_align(s_detail_label, LV_TEXT_ALIGN_CENTER, 0);

    s_session_label = make_label(root, "Project: -\nModel: -", &lv_font_montserrat_16, lv_color_hex(0xf5f5f5));
    lv_obj_set_width(s_session_label, 452);

    s_event_label = make_label(root, "Latest event:\n-", &lv_font_montserrat_16, lv_color_hex(0xa0a0a0));
    lv_obj_set_width(s_event_label, 452);
    lv_obj_set_height(s_event_label, 78);

    s_permission_panel = lv_obj_create(root);
    lv_obj_set_size(s_permission_panel, 452, 210);
    lv_obj_set_style_bg_color(s_permission_panel, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(s_permission_panel, lv_color_hex(0xffb800), 0);
    lv_obj_set_style_border_width(s_permission_panel, 2, 0);
    lv_obj_set_style_radius(s_permission_panel, 8, 0);
    lv_obj_set_style_pad_all(s_permission_panel, 10, 0);
    lv_obj_set_flex_flow(s_permission_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_permission_panel, 8, 0);
    lv_obj_add_flag(s_permission_panel, LV_OBJ_FLAG_HIDDEN);

    s_permission_title = make_label(s_permission_panel, "Permission required", &lv_font_montserrat_20, lv_color_hex(0xffb800));
    lv_obj_set_width(s_permission_title, 430);
    s_permission_detail = make_label(s_permission_panel, "-", &lv_font_montserrat_14, lv_color_hex(0xf5f5f5));
    lv_obj_set_width(s_permission_detail, 430);
    lv_obj_set_height(s_permission_detail, 70);
    lv_label_set_long_mode(s_permission_detail, LV_LABEL_LONG_WRAP);

    lv_obj_t *buttons = lv_obj_create(s_permission_panel);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, 430, 82);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_button(buttons, "ALLOW", lv_color_hex(0x00ff87), allow_event_cb);
    make_button(buttons, "DENY", lv_color_hex(0xff4444), deny_event_cb);

    s_summary_label = make_label(root, "", &lv_font_montserrat_14, lv_color_hex(0x00ff87));
    lv_obj_set_width(s_summary_label, 452);

    ui_unlock();
}

void companion_ui_set_status(companion_status_t status, const char *detail)
{
    static const char *labels[] = {
        "Disconnected",
        "WiFi",
        "Discovering",
        "Connecting",
        "Connected",
        "Authenticated",
        "Error",
    };
    lv_color_t colors[] = {
        lv_color_hex(0x555555),
        lv_color_hex(0x4488ff),
        lv_color_hex(0xffb800),
        lv_color_hex(0xffb800),
        lv_color_hex(0xaa66ff),
        lv_color_hex(0x00ff87),
        lv_color_hex(0xff4444),
    };

    if (!s_status_label || !s_detail_label || !ui_lock(100)) {
        return;
    }
    lv_label_set_text(s_status_label, labels[status]);
    lv_obj_set_style_text_color(s_status_label, colors[status], 0);
    lv_label_set_text(s_detail_label, detail ? detail : "");
    ui_unlock();
}

void companion_ui_render_state(const companion_state_t *state)
{
    char session[256];
    snprintf(session, sizeof(session), "Project: %s\nModel: %s\nState: %s",
             state->project[0] ? state->project : "-",
             state->model[0] ? state->model : "-",
             state->state[0] ? state->state : "-");

    if (!s_session_label || !s_permission_panel || !ui_lock(100)) {
        return;
    }
    lv_label_set_text(s_session_label, session);
    if (strcmp(state->state, "permission_required") != 0) {
        lv_obj_add_flag(s_permission_panel, LV_OBJ_FLAG_HIDDEN);
    }
    ui_unlock();
}

void companion_ui_render_event(const companion_event_t *event)
{
    char text[384];
    snprintf(text, sizeof(text), "Latest event:\n%.64s %.64s\n%.220s",
             event->event,
             event->tool_name,
             event->detail);

    if (!s_event_label || !ui_lock(100)) {
        return;
    }
    lv_label_set_text(s_event_label, text);
    ui_unlock();
}

void companion_ui_render_permission(const companion_permission_t *permission)
{
    strlcpy(s_current_request_id, permission->request_id, sizeof(s_current_request_id));
    char detail[384];
    const char *body = permission->command[0] ? permission->command : permission->message;
    if (!body || !body[0]) {
        body = permission->file_path;
    }
    snprintf(detail, sizeof(detail), "%s\n%s", permission->tool_name, body ? body : "-");

    if (!s_permission_title || !s_permission_detail || !s_permission_panel || !ui_lock(100)) {
        return;
    }
    lv_label_set_text(s_permission_title, "Permission required");
    lv_label_set_text(s_permission_detail, detail);
    lv_obj_clear_flag(s_permission_panel, LV_OBJ_FLAG_HIDDEN);
    ui_unlock();
}

void companion_ui_render_summary(const companion_summary_t *summary)
{
    char text[160];
    snprintf(text, sizeof(text), "Done: %ds, %d events, %d files, %d commands",
             summary->duration_s,
             summary->events_count,
             summary->files_count,
             summary->commands_count);

    if (!s_summary_label || !s_permission_panel || !ui_lock(100)) {
        return;
    }
    lv_label_set_text(s_summary_label, text);
    lv_obj_add_flag(s_permission_panel, LV_OBJ_FLAG_HIDDEN);
    ui_unlock();
}

static void send_action(const char *action)
{
    if (s_action_cb && s_current_request_id[0]) {
        s_action_cb(s_current_request_id, action);
        s_current_request_id[0] = '\0';
    }
}

static void allow_event_cb(lv_event_t *event)
{
    (void)event;
    send_action("allow");
}

static void deny_event_cb(lv_event_t *event)
{
    (void)event;
    send_action("deny");
}
