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
static lv_obj_t *s_project_val_label;
static lv_obj_t *s_model_val_label;
static lv_obj_t *s_event_title_label;
static lv_obj_t *s_event_detail_label;
static lv_obj_t *s_permission_panel;
static lv_obj_t *s_permission_title;
static lv_obj_t *s_permission_detail;
static lv_obj_t *s_summary_label;
static lv_obj_t *s_uptime_label;

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
        .max_transfer_sz = BSP_LCD_H_RES * 80 * BSP_LCD_BITS_PER_PIXEL / 8, // Quadrupled display buffer height for smoother rendering
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
            .buffer_height = 80, // Quadrupled display buffer height
            .use_psram = true,   // Leverage octal PSRAM for display buffer
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
    lv_obj_set_size(button, 175, 64);
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

static void uptime_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t sec = esp_log_timestamp() / 1000;
    uint32_t min = sec / 60;
    uint32_t hour = min / 60;
    min %= 60;
    char buf[32];
    if (hour > 0) {
        snprintf(buf, sizeof(buf), "Up %lu:%02lu", hour, min);
    } else {
        snprintf(buf, sizeof(buf), "Up %02lu:%02lu", min, sec % 60);
    }
    
    // Lock-protected UI label write
    if (s_uptime_label && ui_lock(50)) {
        lv_label_set_text(s_uptime_label, buf);
        ui_unlock();
    }
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
    lv_obj_set_style_pad_row(root, 12, 0);

    // 1. Header (Premium Style)
    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, 452, 30);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(header, "Codex Companion", &lv_font_montserrat_18, lv_color_hex(0xf5f5f5));
    
    lv_obj_t *header_right = lv_obj_create(header);
    lv_obj_remove_style_all(header_right);
    lv_obj_set_flex_flow(header_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(header_right, 10, 0);
    lv_obj_set_flex_align(header_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_uptime_label = make_label(header_right, "Up 00:00", &lv_font_montserrat_14, lv_color_hex(0x888888));
    make_label(header_right, "ESP32", &lv_font_montserrat_14, lv_color_hex(0x4488ff));

    // Divider
    lv_obj_t *div1 = lv_obj_create(root);
    lv_obj_set_size(div1, 452, 1);
    lv_obj_set_style_bg_color(div1, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(div1, 0, 0);

    // 2. Status & Details
    s_status_label = make_label(root, "Booting", &lv_font_montserrat_24, lv_color_hex(0xffb800));
    lv_obj_set_width(s_status_label, 452);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);

    s_detail_label = make_label(root, "Starting display", &lv_font_montserrat_14, lv_color_hex(0xa0a0a0));
    lv_obj_set_width(s_detail_label, 452);
    lv_obj_set_style_text_align(s_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_detail_label, LV_OBJ_FLAG_HIDDEN);

    // Divider
    lv_obj_t *div2 = lv_obj_create(root);
    lv_obj_set_size(div2, 452, 1);
    lv_obj_set_style_bg_color(div2, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(div2, 0, 0);

    // 3. Session Info Panel (Two column layout matching Web Dashboard)
    lv_obj_t *session_panel = lv_obj_create(root);
    lv_obj_remove_style_all(session_panel);
    lv_obj_set_size(session_panel, 452, 60);
    lv_obj_set_flex_flow(session_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(session_panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *proj_card = lv_obj_create(session_panel);
    lv_obj_set_size(proj_card, 218, 56);
    lv_obj_set_style_bg_color(proj_card, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(proj_card, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(proj_card, 1, 0);
    lv_obj_set_style_radius(proj_card, 8, 0);
    lv_obj_set_flex_flow(proj_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(proj_card, 8, 0);
    lv_obj_set_style_pad_row(proj_card, 2, 0);
    make_label(proj_card, "PROJECT", &lv_font_montserrat_12, lv_color_hex(0x888888));
    s_project_val_label = make_label(proj_card, "-", &lv_font_montserrat_16, lv_color_hex(0xffffff));
    lv_obj_set_width(s_project_val_label, 202);

    lv_obj_t *model_card = lv_obj_create(session_panel);
    lv_obj_set_size(model_card, 218, 56);
    lv_obj_set_style_bg_color(model_card, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(model_card, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(model_card, 1, 0);
    lv_obj_set_style_radius(model_card, 8, 0);
    lv_obj_set_flex_flow(model_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(model_card, 8, 0);
    lv_obj_set_style_pad_row(model_card, 2, 0);
    make_label(model_card, "MODEL", &lv_font_montserrat_12, lv_color_hex(0x888888));
    s_model_val_label = make_label(model_card, "-", &lv_font_montserrat_16, lv_color_hex(0xffffff));
    lv_obj_set_width(s_model_val_label, 202);

    // 4. Latest Event Card (Terminal-style dark background)
    lv_obj_t *event_card = lv_obj_create(root);
    lv_obj_set_size(event_card, 452, 94);
    lv_obj_set_style_bg_color(event_card, lv_color_hex(0x151515), 0);
    lv_obj_set_style_border_color(event_card, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(event_card, 1, 0);
    lv_obj_set_style_radius(event_card, 10, 0);
    lv_obj_set_style_pad_all(event_card, 10, 0);
    lv_obj_set_flex_flow(event_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(event_card, 4, 0);

    make_label(event_card, "LATEST EVENT", &lv_font_montserrat_12, lv_color_hex(0x888888));
    s_event_title_label = make_label(event_card, "Waiting", &lv_font_montserrat_16, lv_color_hex(0xffffff));
    lv_obj_set_width(s_event_title_label, 430);
    s_event_detail_label = make_label(event_card, "No events yet", &lv_font_montserrat_14, lv_color_hex(0xaaaaaa));
    lv_obj_set_width(s_event_detail_label, 430);

    // 5. Summary Info
    s_summary_label = make_label(root, "", &lv_font_montserrat_14, lv_color_hex(0x00ff87));
    lv_obj_set_width(s_summary_label, 452);

    // 6. Floating Permission Request Modal (Overlaying the screens)
    s_permission_panel = lv_obj_create(screen);
    lv_obj_set_size(s_permission_panel, 420, 270);
    lv_obj_center(s_permission_panel);
    lv_obj_set_style_bg_color(s_permission_panel, lv_color_hex(0x141414), 0);
    lv_obj_set_style_border_color(s_permission_panel, lv_color_hex(0xffb800), 0);
    lv_obj_set_style_border_width(s_permission_panel, 2, 0);
    lv_obj_set_style_radius(s_permission_panel, 12, 0);
    lv_obj_set_style_pad_all(s_permission_panel, 15, 0);
    lv_obj_set_flex_flow(s_permission_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_permission_panel, 10, 0);
    
    // Add glowing drop shadow to the modal
    lv_obj_set_style_shadow_color(s_permission_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_width(s_permission_panel, 35, 0);
    lv_obj_set_style_shadow_spread(s_permission_panel, 5, 0);
    lv_obj_set_style_shadow_opa(s_permission_panel, LV_OPA_80, 0);

    s_permission_title = make_label(s_permission_panel, "Permission required", &lv_font_montserrat_20, lv_color_hex(0xffb800));
    lv_obj_set_width(s_permission_title, 390);
    
    s_permission_detail = make_label(s_permission_panel, "-", &lv_font_montserrat_14, lv_color_hex(0xf5f5f5));
    lv_obj_set_width(s_permission_detail, 390);
    lv_obj_set_height(s_permission_detail, 90);
    lv_label_set_long_mode(s_permission_detail, LV_LABEL_LONG_WRAP);

    lv_obj_t *buttons = lv_obj_create(s_permission_panel);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, 390, 76);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_button(buttons, "ALLOW", lv_color_hex(0x00ff87), allow_event_cb);
    make_button(buttons, "DENY", lv_color_hex(0xff4444), deny_event_cb);

    lv_obj_add_flag(s_permission_panel, LV_OBJ_FLAG_HIDDEN);

    // Register active uptime timer running every 1 second
    lv_timer_create(uptime_timer_cb, 1000, NULL);
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
        "Retrying",
        "Error",
    };
    lv_color_t colors[] = {
        lv_color_hex(0x555555),
        lv_color_hex(0x4488ff),
        lv_color_hex(0xffb800),
        lv_color_hex(0xffb800),
        lv_color_hex(0xaa66ff),
        lv_color_hex(0x00ff87),
        lv_color_hex(0xffb800),
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
    if (!s_project_val_label || !s_model_val_label || !s_permission_panel || !ui_lock(100)) {
        return;
    }
    lv_label_set_text(s_project_val_label, state->project[0] ? state->project : "-");
    lv_label_set_text(s_model_val_label, state->model[0] ? state->model : "-");

    if (strcmp(state->state, "permission_required") != 0) {
        lv_obj_add_flag(s_permission_panel, LV_OBJ_FLAG_HIDDEN);
    }
    ui_unlock();
}

void companion_ui_render_event(const companion_event_t *event)
{
    if (!s_event_title_label || !s_event_detail_label || !ui_lock(100)) {
        return;
    }
    
    char title[128];
    snprintf(title, sizeof(title), "%.64s %.64s", event->event, event->tool_name);
    lv_label_set_text(s_event_title_label, title);
    lv_label_set_text(s_event_detail_label, event->detail);
    ui_unlock();
}

void companion_ui_render_permission(const companion_permission_t *permission)
{
    char detail[384];
    const char *body = permission->command[0] ? permission->command : permission->message;
    if (!body || !body[0]) {
        body = permission->file_path;
    }
    snprintf(detail, sizeof(detail), "%s\n%s", permission->tool_name, body ? body : "-");

    if (!s_permission_title || !s_permission_detail || !s_permission_panel || !ui_lock(100)) {
        return;
    }
    
    // Fix Bug #1: Store inside the lock context to prevent thread race condition
    strlcpy(s_current_request_id, permission->request_id, sizeof(s_current_request_id));
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
    char request_id[COMPANION_ID_LEN] = {0};

    if (s_action_cb && ui_lock(100)) {
        if (s_current_request_id[0]) {
            strlcpy(request_id, s_current_request_id, sizeof(request_id));
            s_current_request_id[0] = '\0';
        }
        ui_unlock();
    }

    if (s_action_cb && request_id[0]) {
        s_action_cb(request_id, action);
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
