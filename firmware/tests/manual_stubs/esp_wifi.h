#pragma once
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
constexpr int WIFI_IF_STA=0, WIFI_SECOND_CHAN_NONE=0;
inline esp_err_t esp_wifi_get_mac(int,uint8_t *mac) { memset(mac,0,6);mac[0]=2;return ESP_OK; }
inline esp_err_t esp_wifi_set_channel(uint8_t,int) { return ESP_OK; }
