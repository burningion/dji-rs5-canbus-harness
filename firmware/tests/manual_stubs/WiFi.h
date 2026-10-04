#pragma once
#include <string>
constexpr int WIFI_STA=1;
struct FakeWiFi {
  void mode(int) {}
  void setSleep(bool) {}
  std::string macAddress() { return "02:00:00:00:00:01"; }
};
inline FakeWiFi WiFi;
