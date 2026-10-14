#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "wifi_manager.h"
#include "onenet_mqtt.h"

#define TAG "main"

/* >>> 改成你自己的 WiFi。ESP32 只支持 2.4GHz，5GHz 的 SSID 连不上 <<< */
#define WIFI_SSID       "iPhone"
#define WIFI_PASSWORD   "2005511028"

/* 演示用的假数据，验证链路通了就删掉，换成真实传感器读数 */
#define TEST_PUBLISH_PERIOD_MS 4000

static void on_wifi_state(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "wifi up, starting OneNET");
        esp_err_t ret = onenet_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "onenet_start failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "wifi down");
    }
}

static void test_publish_task(void* arg)
{
    (void)arg;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(TEST_PUBLISH_PERIOD_MS));

        if (!onenet_is_connected()) {
            continue;
        }

        /* 这 4 个标识符必须先在 OneNET 控制台的物模型里建好，否则平台会返回错误 */
        onenet_publish_property(
            "{\"ph\":{\"value\":6.66},"
            "\"tds\":{\"value\":318},"
            "\"turbidity\":{\"value\":9.26},"
            "\"temperature\":{\"value\":25.3}}");
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_manager_init(on_wifi_state);
    ESP_ERROR_CHECK(wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD));

    xTaskCreate(test_publish_task, "test_pub", 4096, NULL, 5, NULL);
}
