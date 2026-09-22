#ifndef _ONENET_MQTT_H_
#define _ONENET_MQTT_H_

#include <stdbool.h>
#include "esp_err.h"

/* 创建并启动 MQTT 客户端。非阻塞，返回时只是开始连接。
 * 必须在 WiFi 拿到 IP 之后调用。 */
esp_err_t onenet_start(void);

/* 是否已连上 OneNET，未连上时 publish 会直接丢弃 */
bool onenet_is_connected(void);

/* 上报物模型属性。
 * params 是 OneJSON 的 params 对象内容，例如：
 *   "{\"temperature\":{\"value\":25.3}}"
 * 函数内部会补上 id / version 外壳后发到 thing/property/post。
 * 属性的标识符必须已经在 OneNET 控制台的物模型里创建过，否则平台会返回错误。 */
esp_err_t onenet_publish_property(const char* params);

#endif // _ONENET_MQTT_H_
