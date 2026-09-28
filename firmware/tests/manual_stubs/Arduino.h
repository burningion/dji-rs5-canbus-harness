#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
extern uint32_t fakeNow;
inline uint32_t millis() { return fakeNow; }
inline void delay(unsigned ms) { fakeNow += ms; }
inline void pinMode(uint8_t, int) {}
constexpr int INPUT_PULLUP = 1;
struct FakeSerial {
  std::string written;
  explicit operator bool() const { return true; }
  void begin(int) {}
  int available() const { return 0; }
  char read() { return 0; }
  int availableForWrite() const { return 8192; }
  size_t write(const uint8_t *data, size_t n) { written.append(reinterpret_cast<const char*>(data), n); return n; }
};
extern FakeSerial Serial;
