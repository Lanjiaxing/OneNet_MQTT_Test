#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include <stdbool.h>
#include "esp_err.h"

/* 连接状态回调，只在状态真正变化时触发一次：
 *   connected=true   刚拿到 IP，可以开始跑 TCP/MQTT
 *   connected=false  从"已连接"掉线了（连不上的重试过程不会触发）
 * 断线后驱动会每 5 秒自动重连，不会放弃。 */
typedef void (*p_wifi_state_callback)(bool connected);

/* 初始化 netif / 事件循环 / wifi 驱动，并注册事件处理。
 * 此时 wifi 尚未启动，不占用射频。 */
void wifi_manager_init(p_wifi_state_callback f);

/* 设置 SSID/密码并启动 WiFi。成功后 ip 事件会触发上面的回调。 */
esp_err_t wifi_manager_connect(const char* ssid, const char* password);

#endif // _WIFI_MANAGER_H_
