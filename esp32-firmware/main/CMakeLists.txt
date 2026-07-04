idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "companion_client.c"
        "companion_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_event
        esp_netif
        esp_wifi
        esp_websocket_client
        json
        mdns
        nvs_flash
        waveshare__esp32_s3_touch_amoled_2_16
        lvgl__lvgl
)
