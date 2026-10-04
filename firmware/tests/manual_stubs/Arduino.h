#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <stdarg.h>
#include <stdio.h>
using portMUX_TYPE = int;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0;
inline void portENTER_CRITICAL(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL(portMUX_TYPE *) {}
extern uint32_t fakeNow;
inline uint32_t millis() { return fakeNow; }
inline void delay(unsigned ms) { fakeNow += ms; }
inline void pinMode(uint8_t, int) {}
constexpr int INPUT_PULLUP = 1;
constexpr int SERIAL_8N1 = 0;
enum hardwareSerial_error_t { UART_NO_ERROR, UART_FIFO_OVF_ERROR };
struct FakeSerial {
  std::string written, incoming;
  bool connected = true;
  explicit operator bool() const { return connected; }
  void begin(int) {}
  void begin(int, int, int, int) {}
  void setRxBufferSize(unsigned) {}
  void onReceiveError(void (*)(hardwareSerial_error_t)) {}
  void printf(const char *format, ...) {
    char buffer[512]; va_list args; va_start(args, format);
    vsnprintf(buffer,sizeof(buffer),format,args); va_end(args); written += buffer;
  }
  int available() const { return static_cast<int>(incoming.size()); }
  char read() { const char c = incoming.front(); incoming.erase(0, 1); return c; }
  int availableForWrite() const { return 8192; }
  size_t write(const uint8_t *data, size_t n) { written.append(reinterpret_cast<const char*>(data), n); return n; }
};
extern FakeSerial Serial;
extern FakeSerial Serial1;
