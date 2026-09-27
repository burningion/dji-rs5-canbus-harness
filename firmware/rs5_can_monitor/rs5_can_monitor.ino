// RS5 CAN monitor. Arduino-ESP32 3.3.11, Adafruit Feather ESP32-S3 No PSRAM.
// Feather 5 -> Waveshare CAN TX; 6 <- CAN RX; 3V -> 3.3V; GND -> GND.
// Connect CANH/CANL and common ground only after verifying the RS5 contacts.
// This sketch has no CAN transmit path and never changes out of listen-only.

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_err.h>
#include <inttypes.h>
#include <stdarg.h>
#include "ConsoleQueue.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select an ESP32-S3 board. This sketch's wiring is for ESP32-S3."
#endif

static constexpr gpio_num_t CAN_TX_PIN = GPIO_NUM_5;
static constexpr gpio_num_t CAN_RX_PIN = GPIO_NUM_6;
static constexpr uint32_t REPORT_INTERVAL_MS = 1000;
static constexpr uint32_t FRAME_LINES_PER_SECOND = 20;
static constexpr uint32_t ALERTS = TWAI_ALERT_BUS_ERROR |
                                   TWAI_ALERT_RX_QUEUE_FULL |
                                   TWAI_ALERT_RX_FIFO_OVERRUN |
                                   TWAI_ALERT_ERR_PASS |
                                   TWAI_ALERT_BUS_OFF;

static bool ready = false;
static bool showFrames = true;
static const char *failedOperation = "not initialized";
static esp_err_t failure = ESP_OK;
static uint32_t lastReportMs = 0;
static uint32_t frameWindowMs = 0;
static uint32_t frameLines = 0;
static uint32_t pendingAlerts = 0;
static uint64_t received = 0;
static uint64_t id222 = 0;
static uint64_t id223 = 0;
static uint64_t omittedFrameLines = 0;
static uint64_t skippedConsoleLines = 0;
static ConsoleQueue<4096> consoleQueue;

// Best-effort output: an absent/slow serial monitor must not stall CAN draining.
static void logLine(const char *format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length <= 0 || length >= static_cast<int>(sizeof(line))) {
    ++skippedConsoleLines;
    return;
  }
  if (!Serial || !consoleQueue.append(line, static_cast<size_t>(length))) {
    ++skippedConsoleLines;
  }
}

static void drainConsole() {
  if (!Serial || consoleQueue.size() == 0) return;
  const int available = Serial.availableForWrite();
  if (available <= 0) return;
  size_t length = consoleQueue.contiguousSize();
  if (length > static_cast<size_t>(available)) length = available;
  // Feather TinyUSB can expose only 64 writable bytes at a time. Partial writes
  // remain queued, so long diagnostic lines are neither discarded nor blocking.
  consoleQueue.consume(Serial.write(consoleQueue.data(), length));
}

static void printHelp() {
  logLine("RS5 CAN monitor | ESP32-S3 | TX=GPIO%d RX=GPIO%d\n",
          static_cast<int>(CAN_TX_PIN), static_cast<int>(CAN_RX_PIN));
  logLine("LISTEN ONLY | Classical CAN 1000000 bit/s | all IDs accepted | no TX/ACK/error frames\n");
  logLine("Commands: h/? help, s status, p toggle frame previews. No transmit commands.\n");
  logLine("Frame previews: at most 20/s. Counters continue when previews are omitted.\n");
  logLine("Quiet bus is inconclusive; this listener cannot acknowledge another node.\n");
}

static const char *stateName(twai_state_t state) {
  switch (state) {
    case TWAI_STATE_STOPPED: return "STOPPED";
    case TWAI_STATE_RUNNING: return "RUNNING";
    case TWAI_STATE_BUS_OFF: return "BUS_OFF";
    case TWAI_STATE_RECOVERING: return "RECOVERING";
    default: return "UNKNOWN";
  }
}

static void printStatus() {
  if (!ready) {
    logLine("DRIVER FAILED: %s: %s. Check board selection/configuration and reset.\n",
            failedOperation, esp_err_to_name(failure));
    return;
  }
  twai_status_info_t status = {};
  const esp_err_t result = twai_get_status_info(&status);
  if (result != ESP_OK) {
    logLine("Status failed: %s\n", esp_err_to_name(result));
    return;
  }
  logLine("[%" PRIu32 " ms] LISTEN_ONLY %s rx=%" PRIu64 " id222=%" PRIu64 " id223=%" PRIu64 "\n",
          static_cast<uint32_t>(millis()), stateName(status.state), received, id222, id223);
  logLine("  bus_errors=%" PRIu32 " rx_missed=%" PRIu32 " fifo_overrun=%" PRIu32
          " REC=%" PRIu32 " TEC=%" PRIu32 " queued=%" PRIu32 " alerts=0x%08" PRIX32 "\n",
          status.bus_error_count, status.rx_missed_count, status.rx_overrun_count,
          status.rx_error_counter, status.tx_error_counter, status.msgs_to_rx, pendingAlerts);
  logLine("  previews=%s omitted=%" PRIu64 " console_skipped=%" PRIu64 "\n",
          showFrames ? "on" : "off", omittedFrameLines, skippedConsoleLines);
  if (received == 0) {
    logLine("  No valid frames yet; this does not establish whether RS5 wiring is correct.\n");
  }
  pendingAlerts = 0;
}

static void processFrame(const twai_message_t &frame) {
  ++received;
  if (!frame.extd && !frame.rtr) {
    if (frame.identifier == 0x222) ++id222;
    if (frame.identifier == 0x223) ++id223;
  }
  const uint32_t now = millis();
  if (now - frameWindowMs >= REPORT_INTERVAL_MS) {
    frameWindowMs = now;
    frameLines = 0;
  }
  if (!showFrames || frameLines >= FRAME_LINES_PER_SECOND) {
    ++omittedFrameLines;
    return;
  }
  ++frameLines;
  char data[25] = {};
  if (!frame.rtr) {
    // Classical CAN has at most eight data bytes, even with a non-compliant DLC.
    const uint8_t count = frame.data_length_code > 8 ? 8 : frame.data_length_code;
    for (uint8_t i = 0; i < count; ++i) {
      snprintf(data + i * 3, sizeof(data) - i * 3, "%02X ", frame.data[i]);
    }
  }
  // Timestamp is when the application dequeues the frame, not a hardware timestamp.
  logLine("[%" PRIu32 " ms] %s ID=0x%0*" PRIX32 " %s DLC=%u %s\n",
          now, frame.extd ? "EXT" : "STD", frame.extd ? 8 : 3,
          frame.identifier, frame.rtr ? "RTR" : "DATA", frame.data_length_code, data);
}

void setup() {
  // HIGH is recessive at the transceiver's TX input. Establish it before USB wait.
  gpio_set_level(CAN_TX_PIN, 1);
  gpio_set_direction(CAN_TX_PIN, GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(CAN_TX_PIN, GPIO_PULLUP_ONLY);
  pinMode(static_cast<uint8_t>(CAN_RX_PIN), INPUT_PULLUP);

#if !ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MODE
  Serial.setTxBufferSize(2048);
#endif
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  const uint32_t waitStarted = millis();
  while (!Serial && millis() - waitStarted < 2000) delay(10);

  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
      CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
  general.tx_queue_len = 0;
  general.rx_queue_len = 256;
  general.alerts_enabled = ALERTS;
  const twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
  const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  failedOperation = "twai_driver_install";
  failure = twai_driver_install(&general, &timing, &filter);
  if (failure == ESP_OK) {
    failedOperation = "twai_start";
    failure = twai_start();
    ready = failure == ESP_OK;
    if (!ready) {
      twai_driver_uninstall();
      gpio_set_level(CAN_TX_PIN, 1);
      gpio_set_direction(CAN_TX_PIN, GPIO_MODE_OUTPUT);
    }
  }
  printHelp();
  printStatus();
  lastReportMs = millis();
}

void loop() {
  if (ready) {
    // Bound each batch so a busy bus cannot starve status, console, or watchdog.
    for (uint16_t i = 0; i < 64; ++i) {
      twai_message_t frame = {};
      const esp_err_t result = twai_receive(&frame, 0);
      if (result == ESP_ERR_TIMEOUT) break;
      if (result != ESP_OK) {
        failedOperation = "twai_receive";
        failure = result;
        ready = false;
        break;
      }
      processFrame(frame);
    }
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) == ESP_OK) pendingAlerts |= alerts;
  }

  // Single-character commands only affect console output, never the CAN mode.
  for (uint8_t i = 0; i < 16 && Serial.available(); ++i) {
    switch (Serial.read()) {
      case 'h': case '?': printHelp(); break;
      case 's': printStatus(); break;
      case 'p':
        showFrames = !showFrames;
        logLine("Frame previews %s; reception continues.\n", showFrames ? "on" : "off");
        break;
      default: break;
    }
  }
  const uint32_t now = millis();
  if (now - lastReportMs >= REPORT_INTERVAL_MS) {
    lastReportMs = now;
    printStatus();
  }
  drainConsole();
  delay(1);
}
