#include "companion_client.h"
#include "companion_ui.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_manager.h"

static const char *TAG = "main";

void app_main(void)
{
    companion_ui_init(companion_client_send_action);
    companion_ui_set_status(COMPANION_STATUS_WIFI, "Initializing NVS");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    companion_ui_set_status(COMPANION_STATUS_WIFI, "Connecting WiFi");
    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi failed: %s", esp_err_to_name(ret));
        companion_ui_set_status(COMPANION_STATUS_ERROR, "WiFi failed");
        return;
    }

    ret = companion_client_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Companion client failed: %s", esp_err_to_name(ret));
        companion_ui_set_status(COMPANION_STATUS_ERROR, "Companion connect failed");
        return;
    }
}
