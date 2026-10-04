#pragma once
#include <vector>
#include <stdint.h>
#include "esp_err.h"
struct esp_now_recv_info_t { uint8_t *src_addr; };
struct esp_now_peer_info_t {
  uint8_t peer_addr[6], lmk[16], channel;
  int ifidx; bool encrypt;
};
using esp_now_recv_cb_t=void (*)(const esp_now_recv_info_t*,const uint8_t*,int);
inline std::vector<uint8_t> radioSent;
inline esp_err_t radioSendResult=ESP_OK;
inline esp_err_t esp_now_init() { return ESP_OK; }
inline esp_err_t esp_now_set_pmk(const uint8_t *) { return ESP_OK; }
inline esp_err_t esp_now_add_peer(const esp_now_peer_info_t *) { return ESP_OK; }
inline esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t) { return ESP_OK; }
inline esp_err_t esp_now_send(const uint8_t *,const uint8_t *p,size_t n) {
  radioSent.assign(p,p+n); return radioSendResult;
}
