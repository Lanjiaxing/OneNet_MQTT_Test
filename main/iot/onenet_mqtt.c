#include "onenet_mqtt.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "mqtt_client.h"

#define TAG "onenet"

/* ---------------- OneNET 设备三元组 ---------------- */

#define ONENET_PRODUCT_ID   "u5fr05Pj07"
#define ONENET_DEVICE_NAME  "ESP32_02"

/* 非加密接入。加密接入改为 "mqtts://mqttstls.heclouds.com:8883" 并配置 CA 证书 */
#define ONENET_BROKER_URI   "mqtt://mqtts.heclouds.com:1883"

/* Token 由设备密钥 + 过期时间算出，et=1910271119 对应 2030-07-14。
 * 过期后需要重新生成（OneNET 控制台 → 设备 → 详情 → 鉴权信息，或用 token 生成工具）。 */
#define ONENET_TOKEN \
    "version=2018-10-31&res=products%2Fu5fr05Pj07%2Fdevices%2FESP32_02" \
    "&et=1910271119&method=md5&sign=VifR7YrvGvzV1UQwl%2BrxzA%3D%3D"

/* ---------------- 物模型主题 ---------------- */

#define TOPIC_PROPERTY_POST  "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post"
#define TOPIC_POST_REPLY     "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post/reply"
#define TOPIC_PROPERTY_SET   "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set"
#define TOPIC_SET_REPLY      "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set_reply"

/* 物模型属性的最大取值范围与用途无关，这里限制的是单条 OneJSON 报文长度 */
#define PAYLOAD_MAX 512
#define TOPIC_MAX   160

static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;
static int s_msg_id = 0;

bool onenet_is_connected(void)
{
    return s_connected;
}

/* 从平台下发的 JSON 里取出 "id" 字段，set_reply 必须把它原样带回去 */
static void extract_json_id(const char* json, char* out, size_t out_sz)
{
    const char* p = strstr(json, "\"id\"");
    const char* q = NULL;
    size_t i = 0;

    out[0] = '\0';
    if (!p) {
        return;
    }
    q = strchr(p + 4, ':');
    if (!q) {
        return;
    }
    q++;
    while (*q == ' ' || *q == '"') {
        q++;
    }
    while (*q && *q != '"' && *q != ',' && i < out_sz - 1) {
        out[i++] = *q++;
    }
    out[i] = '\0';
}

static void handle_property_set(const char* json)
{
    char id[16] = { 0 };
    char reply[64];

    extract_json_id(json, id, sizeof(id));
    if (id[0] == '\0') {
        ESP_LOGW(TAG, "property/set without id, ignored");
        return;
    }

    /* 这里只是应答平台说"收到了"。真正要控制水泵 / 加热时，
     * 应该在这一步解析 params 里的值并驱动 GPIO。 */
    snprintf(reply, sizeof(reply), "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}", id);
    esp_mqtt_client_publish(s_client, TOPIC_SET_REPLY, reply, 0, 0, 0);
    ESP_LOGI(TAG, "set_reply -> %s", reply);
}

static void mqtt_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "connected to OneNET");
        esp_mqtt_client_subscribe(s_client, TOPIC_POST_REPLY, 0);
        esp_mqtt_client_subscribe(s_client, TOPIC_PROPERTY_SET, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "disconnected from OneNET");
        break;

    case MQTT_EVENT_DATA: {
        /* ev->topic / ev->data 不是以 '\0' 结尾的，长度分别看 topic_len / data_len */
        char topic[TOPIC_MAX];
        char data[PAYLOAD_MAX];
        size_t tlen = ev->topic_len < (int)sizeof(topic) - 1 ? ev->topic_len : (int)sizeof(topic) - 1;
        size_t dlen = ev->data_len < (int)sizeof(data) - 1 ? ev->data_len : (int)sizeof(data) - 1;

        memcpy(topic, ev->topic, tlen);
        topic[tlen] = '\0';
        memcpy(data, ev->data, dlen);
        data[dlen] = '\0';

        ESP_LOGI(TAG, "RX [%s] %s", topic, data);

        if (strstr(topic, "/thing/property/set")) {
            handle_property_set(data);
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "transport error, sock errno=%d",
                     ev->error_handle->esp_transport_sock_errno);
        } else if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            /* 连上 TCP 但被平台拒绝，几乎都是 client_id / username / password 不对 */
            ESP_LOGE(TAG, "connection refused, check client_id/username/token");
        } else {
            ESP_LOGE(TAG, "mqtt error, type=%d", ev->error_handle->error_type);
        }
        break;

    default:
        break;
    }
}

esp_err_t onenet_start(void)
{
    if (s_client) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_mqtt_client_config_t cfg = { 0 };
    cfg.broker.address.uri = ONENET_BROKER_URI;
    cfg.credentials.client_id = ONENET_DEVICE_NAME;              /* 设备名称 */
    cfg.credentials.username = ONENET_PRODUCT_ID;                /* 产品ID */
    cfg.credentials.authentication.password = ONENET_TOKEN;      /* Token */
    cfg.session.keepalive = 60;
    cfg.network.reconnect_timeout_ms = 5000;

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register_event failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "client start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "mqtt client started, connecting to %s", ONENET_BROKER_URI);
    return ESP_OK;
}

esp_err_t onenet_publish_property(const char* params)
{
    char payload[PAYLOAD_MAX];
    int len = 0;
    int published = 0;

    if (!s_client) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!params) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_connected) {
        ESP_LOGW(TAG, "not connected, publish dropped");
        return ESP_ERR_INVALID_STATE;
    }

    len = snprintf(payload, sizeof(payload),
                   "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":%s}",
                   ++s_msg_id, params);
    if (len < 0 || len >= (int)sizeof(payload)) {
        ESP_LOGE(TAG, "payload too long (%d bytes)", len);
        return ESP_ERR_INVALID_SIZE;
    }

    published = esp_mqtt_client_publish(s_client, TOPIC_PROPERTY_POST, payload, 0, 1, 0);
    if (published < 0) {
        ESP_LOGE(TAG, "publish failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TX %s", payload);
    return ESP_OK;
}
