#include "wifi_manager.h"

#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_timer.h"

#define TAG "wifi_manager"

/* 断线后每隔这么久重试一次，直到连上为止，不设放弃次数 */
#define RECONNECT_INTERVAL_US (5 * 1000 * 1000)

static int connect_attempt = 0;
static bool is_connected = false;
static p_wifi_state_callback wifi_state_cb = NULL;
static esp_timer_handle_t reconnect_timer = NULL;

/* 跑在 esp_timer 自己的任务里，不占用事件循环任务 */
static void reconnect_timer_cb(void* arg)
{
    ESP_LOGI(TAG, "retrying connect (attempt %d)", ++connect_attempt);
    esp_wifi_connect();
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting to AP...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "associated with AP, waiting for IP...");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t* d = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG, "disconnected, reason=%d", d->reason);

            /* 只在"连上过又掉了"时通知上层。连不上的失败尝试不算状态变化，
             * 否则重试期间会刷一屏的 wifi down。 */
            if (is_connected) {
                is_connected = false;
                if (wifi_state_cb) {
                    wifi_state_cb(false);
                }
            }

            /* 启动周期重连。已经在跑就交给它，重复 start 会返回
             * ESP_ERR_INVALID_STATE，所以先判断。 */
            if (reconnect_timer && !esp_timer_is_active(reconnect_timer)) {
                ESP_ERROR_CHECK(esp_timer_start_periodic(reconnect_timer, RECONNECT_INTERVAL_US));
            }
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* e = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&e->ip_info.ip));

        connect_attempt = 0;
        is_connected = true;

        if (reconnect_timer && esp_timer_is_active(reconnect_timer)) {
            esp_timer_stop(reconnect_timer);
        }
        if (wifi_state_cb) {
            wifi_state_cb(true);
        }
    }
}

void wifi_manager_init(p_wifi_state_callback f)
{
    wifi_state_cb = f;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name = "wifi_reconn",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &reconnect_timer));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

esp_err_t wifi_manager_connect(const char* ssid, const char* password)
{
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wc = { 0 };
    snprintf((char*)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", ssid);
    snprintf((char*)wc.sta.password, sizeof(wc.sta.password), "%s", password ? password : "");
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    return esp_wifi_start();
}
