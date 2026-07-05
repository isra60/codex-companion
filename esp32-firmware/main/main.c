#include "companion_client.h"
#include "companion_ui.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

    int retries = 0;
    const int max_retries = 5;
    uint32_t delay_ms = 3000;

    while (retries < max_retries) {
        companion_ui_set_status(COMPANION_STATUS_DISCOVERING, "Connecting companion...");
        ret = companion_client_start();
        if (ret == ESP_OK) {
            break;
        }

        retries++;
        char detail[64];
        if (retries < max_retries) {
            snprintf(detail, sizeof(detail), "Failed. Retry %d/%d in %lus", retries, max_retries, (unsigned long)(delay_ms / 1000));
            companion_ui_set_status(COMPANION_STATUS_RETRYING, detail);
            ESP_LOGW(TAG, "Companion client failed: %s. Retrying in %lu ms...", esp_err_to_name(ret), (unsigned long)delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            delay_ms *= 2; // Exponential backoff
            if (delay_ms > 30000) {
                delay_ms = 30000;
            }
        }
    }

    if (retries >= max_retries) {
        ESP_LOGE(TAG, "Companion client failed after max retries. Rebooting...");
        companion_ui_set_status(COMPANION_STATUS_ERROR, "Connect failed. Rebooting in 5s...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
}
