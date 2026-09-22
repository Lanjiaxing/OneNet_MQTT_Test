idf_component_register(
    SRCS "main.c" "iot/wifi_manager.c" "iot/onenet_mqtt.c"
    INCLUDE_DIRS "." "iot"
    PRIV_REQUIRES esp_wifi esp_netif esp_event log nvs_flash
)
