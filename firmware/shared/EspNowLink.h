#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "WirelessProtocol.h"

#if __has_include("WirelessConfig.h")
#include "WirelessConfig.h"
#else
namespace wirelessConfig {
constexpr uint8_t CameraMac[6]={}, BodyMac[6]={}, Pmk[16]={}, Lmk[16]={};
constexpr uint8_t Channel=6;
}
#endif

namespace espnowLink {
struct Message { uint8_t bytes[wireless::ResponseSize]; size_t size=0; };
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static Message inbox;
static bool waiting=false, overflow=false, ready=false;
static uint8_t peerMac[6];
static bool nonzero(const uint8_t *p, size_t n) {
  for (size_t i=0;i<n;++i) if (p[i]) return true;
  return false;
}
// Wi-Fi task only copies bounded data. Parsing, printing and CAN run in loop().
static void receive(const esp_now_recv_info_t *info, const uint8_t *data, int size) {
  if (!info || memcmp(info->src_addr,peerMac,6) || !data || size<=0 ||
      size>static_cast<int>(sizeof(inbox.bytes))) return;
  portENTER_CRITICAL(&mux);
  if (waiting) overflow=true;
  memcpy(inbox.bytes,data,size); inbox.size=static_cast<size_t>(size); waiting=true;
  portEXIT_CRITICAL(&mux);
}
static bool begin(bool camera) {
  ready=false;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  uint8_t local[6]={};
  if (esp_wifi_get_mac(WIFI_IF_STA,local)!=ESP_OK) return false;
  Serial.printf("ESP-NOW %s STA MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
      camera ? "camera" : "body",local[0],local[1],local[2],local[3],local[4],local[5]);
  const uint8_t *expected=camera ? wirelessConfig::CameraMac : wirelessConfig::BodyMac;
  memcpy(peerMac,camera ? wirelessConfig::BodyMac : wirelessConfig::CameraMac,6);
  if (memcmp(local,expected,6) || !nonzero(peerMac,6) ||
      !nonzero(wirelessConfig::Pmk,16) || !nonzero(wirelessConfig::Lmk,16) ||
      wirelessConfig::Channel<1 || wirelessConfig::Channel>11) return false;
  if (esp_wifi_set_channel(wirelessConfig::Channel,WIFI_SECOND_CHAN_NONE)!=ESP_OK ||
      esp_now_init()!=ESP_OK || esp_now_set_pmk(wirelessConfig::Pmk)!=ESP_OK) return false;
  esp_now_peer_info_t peer={};
  memcpy(peer.peer_addr,peerMac,6); memcpy(peer.lmk,wirelessConfig::Lmk,16);
  peer.channel=wirelessConfig::Channel; peer.ifidx=WIFI_IF_STA; peer.encrypt=true;
  if (esp_now_add_peer(&peer)!=ESP_OK || esp_now_register_recv_cb(receive)!=ESP_OK) return false;
  ready=true; return true;
}
static bool take(Message &message, bool &lost) {
  portENTER_CRITICAL(&mux);
  const bool available=waiting;
  if (available) message=inbox;
  lost=overflow; waiting=false; overflow=false;
  portEXIT_CRITICAL(&mux);
  return available;
}
static bool send(const uint8_t *data, size_t size) {
  return ready && esp_now_send(peerMac,data,size)==ESP_OK;
}
} // namespace espnowLink
