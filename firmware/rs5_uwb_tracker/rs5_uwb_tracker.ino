// Adafruit Feather ESP32-S3, 8 MB/no PSRAM, Makerfabs Gen1 STM32 AoA, DJI RS5.
// Anchor TXD1/PA9 -> Feather RX/GPIO38; anchor GND -> Feather GND.
// Waveshare CAN TX <- Feather 5; CAN RX -> Feather 6; 3.3V <- Feather 3V.
// Anchor and Feather each use their own USB supply; only signal grounds join.

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_err.h>
#include <inttypes.h>
#include <stdarg.h>
#include "ConsoleQueue.h"
#include "DjiProtocol.h"
#include "MakerfabsProtocol.h"
#include "TrackingCore.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "This firmware targets the Adafruit Feather ESP32-S3 No PSRAM."
#endif
#ifndef RS5_ENABLE_MOTION
#define RS5_ENABLE_MOTION 0
#endif
#if RS5_ENABLE_MOTION != 0 && RS5_ENABLE_MOTION != 1
#error "RS5_ENABLE_MOTION must be 0 or 1."
#endif

static constexpr gpio_num_t CanTx = GPIO_NUM_5, CanRx = GPIO_NUM_6;
static constexpr int UwbRx = 38, UnconnectedUartTx = 39;
static constexpr uint32_t ControlPeriodMs = 50, QueryPeriodMs = 200;
static constexpr uint32_t JointTimeoutMs = 500;
static constexpr float JointLimitDegrees = 85.0f;
static constexpr uint32_t CanAlerts = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF |
                                      TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN |
                                      TWAI_ALERT_TX_FAILED;

static tracking::Tracker tracker;
static uwb::StreamParser uwbParser;
static dji::StreamParser djiParser;
static ConsoleQueue<4096> consoleQueue;
static bool canReady = false, activeCan = false, armed = false;
static bool haveJoint = false, queryPending = false;
static int direction = 1;
static uint16_t packetSequence = 0, querySequence = 0;
static uint32_t querySentMs = 0, jointReceivedMs = 0;
static uint32_t lastControlMs = 0, lastReportMs = 0, lastLoopMs = 0;
static uint32_t uwbReports = 0, badReports = 0, canFrames = 0;
static uint32_t droppedLogs = 0;
static dji::JointAngles joint;
static float requestedSpeed = 0;
static volatile bool uartError = false;
static char commandLine[64];
static size_t commandLength = 0;
static bool discardCommand = false;

static void logLine(const char *format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length <= 0 || length >= static_cast<int>(sizeof(line)) ||
      !Serial || !consoleQueue.append(line, static_cast<size_t>(length))) ++droppedLogs;
}

static void drainConsole() {
  if (!Serial || !consoleQueue.size()) return;
  const int available = Serial.availableForWrite();
  if (available <= 0) return;
  size_t length = consoleQueue.contiguousSize();
  if (length > static_cast<size_t>(available)) length = available;
  consoleQueue.consume(Serial.write(consoleQueue.data(), length));
}

static void stopDriver() {
  if (canReady) {
    twai_stop();
    twai_driver_uninstall();
  }
  canReady = activeCan = armed = haveJoint = queryPending = false;
  djiParser.reset();
  gpio_set_level(CanTx, 1);
  gpio_set_direction(CanTx, GPIO_MODE_OUTPUT);
}

static void canFault(const char *operation, esp_err_t error) {
  // Stopping clears queued commands. No automatic retry or motion re-arming.
  stopDriver();
  requestedSpeed = 0;
  logLine("CAN FAULT %s: %s; driver stopped, motion disarmed. Fix wiring before probe/listen.\n",
          operation, esp_err_to_name(error));
}

static bool startCan(bool active) {
  stopDriver();
#if !RS5_ENABLE_MOTION
  if (active) return false;
#endif
  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
      CanTx, CanRx, active ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY);
  general.tx_queue_len = active ? 8 : 0;
  general.rx_queue_len = 64;
  general.alerts_enabled = CanAlerts;
  const twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
  const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t result = twai_driver_install(&general, &timing, &filter);
  if (result != ESP_OK) { canFault("install", result); return false; }
  canReady = true;
  result = twai_start();
  if (result != ESP_OK) { canFault("start", result); return false; }
  activeCan = active;
  logLine("CAN %s at 1 Mbit/s. Motion is DISARMED.\n", active ? "QUERY MODE (normal CAN)" : "LISTEN ONLY");
  return true;
}

static bool sendPacket(const dji::Packet &packet) {
#if RS5_ENABLE_MOTION
  if (!canReady || !activeCan || packet.length == 0) return false;
  for (size_t offset = 0; offset < packet.length; offset += 8) {
    twai_message_t frame = {};
    frame.identifier = dji::TxCanId;
    frame.ss = 1;  // Each CAN frame gets one attempt; never queue endless retries.
    const size_t remaining = packet.length - offset;
    frame.data_length_code = remaining > 8 ? 8 : remaining;
    memcpy(frame.data, packet.bytes + offset, frame.data_length_code);
    const esp_err_t result = twai_transmit(&frame, pdMS_TO_TICKS(5));
    if (result != ESP_OK) { canFault("transmit", result); return false; }
  }
  return true;
#else
  (void)packet;
  return false;
#endif
}

static void disarm(const char *reason) {
  const bool wasArmed = armed;
  armed = false;
  if (wasArmed && activeCan) {
    // Zero all speeds, then release speed control in CAN bus order.
    if (sendPacket(dji::yawSpeed(++packetSequence, 0, true))) {
      sendPacket(dji::yawSpeed(++packetSequence, 0, false));
    }
  }
  if (wasArmed) logLine("DISARMED: %s. Re-arm explicitly after checking the cause.\n", reason);
}

static bool jointFresh(uint32_t now) {
  return haveJoint && now - jointReceivedMs <= JointTimeoutMs;
}

static void printHelp() {
  logLine("RS5 UWB tracker | Feather ESP32-S3 No PSRAM | CAN TX=5 RX=6 | UWB RX=38 (RX pad)\n");
  logLine("Build=%s. Startup always listen-only/disarmed. Anchor must turn with lens.\n",
          RS5_ENABLE_MOTION ? "MOTION-CAPABLE" : "PREVIEW ONLY");
  logLine("Commands (newline): tag HHHH | status | help | stop | listen | probe | arm + | arm -\n");
  logLine("tag selects the anchor's a16. probe sends joint-angle queries in a motion-capable build.\n");
  logLine("arm +/- selects calibrated yaw direction. Loss of tag/CAN telemetry requires re-arming.\n");
}

static void printStatus(uint32_t now) {
  logLine("%s tag=%s%04X uwb=%" PRIu32 " bad=%" PRIu32 " state=%s bearing=%.1f range=%.2fm proposed=%.1fdeg/s\n",
          armed ? "ARMED" : "DISARMED", tracker.selected() ? "" : "unset/",
          tracker.tag(), uwbReports, badReports, tracker.reason(), tracker.bearing(),
          tracker.distanceCm() / 100.0f, requestedSpeed);
  logLine("CAN=%s frames=%" PRIu32 " joint=%s yaw=%.1f skipped_logs=%" PRIu32 "\n",
          canReady ? (activeCan ? "query/normal" : "listen-only") : "stopped",
          canFrames, jointFresh(now) ? "fresh" : "missing/stale", joint.yaw, droppedLogs);
}

static void handleCommand(const char *line, uint32_t now) {
  if (strcmp(line, "help") == 0) { printHelp(); return; }
  if (strcmp(line, "status") == 0) { printStatus(now); return; }
  if (strcmp(line, "stop") == 0) { disarm("operator stop"); return; }
  if (strcmp(line, "listen") == 0) {
    disarm("listen-only requested");
    // Allow the zero/release packets a short bounded interval before clearing TX.
    if (activeCan) delay(5);
    startCan(false);
    return;
  }
  if (strlen(line) == 8 && strncmp(line, "tag ", 4) == 0) {
    uint16_t tag = 0;
    for (unsigned i = 4; i < 8; ++i) {
      const unsigned char c = line[i];
      if (!isxdigit(c)) { logLine("Use tag followed by exactly four hex digits.\n"); return; }
      tag = static_cast<uint16_t>((tag << 4) | (isdigit(c) ? c - '0' : toupper(c) - 'A' + 10));
    }
    disarm("tag changed");
    tracker.select(tag);
    logLine("Selected tag %04X; waiting for three fresh reports.\n", tag);
    return;
  }
  if (strcmp(line, "probe") == 0) {
#if RS5_ENABLE_MOTION
    disarm("query mode requested");
    if (activeCan) delay(5);
    startCan(true);
#else
    logLine("Preview build: probe and arm are disabled. See tracking setup before enabling motion.\n");
#endif
    return;
  }
  if (strcmp(line, "arm +") == 0 || strcmp(line, "arm -") == 0) {
#if RS5_ENABLE_MOTION
    if (!activeCan || !tracker.fresh(now) || !jointFresh(now) ||
        fabsf(joint.yaw) >= JointLimitDegrees) {
      logLine("Cannot arm: require probe mode, three fresh selected-tag reports, and fresh joint yaw inside +/-85deg.\n");
      return;
    }
    if (armed) { logLine("Already armed. Send stop before changing direction.\n"); return; }
    direction = line[4] == '-' ? -1 : 1;
    tracker.restartOutput(now);
    lastControlMs = now;
    armed = true;
    logLine("ARMED direction=%+d max=15deg/s; keep stop available.\n", direction);
#else
    logLine("Preview build: motion disabled.\n");
#endif
    return;
  }
  logLine("Unknown command. Send help.\n");
}

static void readCommands(uint32_t now) {
  for (unsigned i = 0; i < 128 && Serial.available(); ++i) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (!discardCommand && commandLength) {
        commandLine[commandLength] = '\0';
        handleCommand(commandLine, now);
      }
      commandLength = 0;
      discardCommand = false;
    } else if (!discardCommand) {
      if (c < 32 || c > 126 || commandLength + 1 >= sizeof(commandLine)) {
        discardCommand = true;
        disarm("invalid console command");
      } else commandLine[commandLength++] = c;
    }
  }
}

static void readUwb(uint32_t now) {
  if (uartError || Serial1.available() > 512) {
    uartError = false;
    // Reject buffered history after a receive overrun; don't call it a fresh fix.
    for (unsigned i = 0; i < 2048 && Serial1.available(); ++i) Serial1.read();
    uwbParser.reset();
    tracker.invalidate("UWB serial overrun");
    disarm("UWB serial overrun");
    ++badReports;
    return;
  }
  if (uwbParser.expire(now)) {
    tracker.invalidate("incomplete UWB report");
    disarm("incomplete UWB report");
    ++badReports;
  }
  for (unsigned i = 0; i < 256 && Serial1.available(); ++i) {
    uwb::Measurement m;
    const uwb::ParseResult result = uwbParser.feed(Serial1.read(), now, m);
    if (result == uwb::ParseResult::Invalid) {
      ++badReports;
      tracker.invalidate("invalid UWB report");
      disarm("invalid UWB report");
    } else if (result == uwb::ParseResult::Measurement) {
      ++uwbReports;
      if (!tracker.selected() && uwbReports % 10 == 1) {
        logLine("Observed tag %04X. Select explicitly with: tag %04X\n", m.tag, m.tag);
      }
      tracker.update(m, now);
      if (armed && !tracker.fresh(now)) disarm(tracker.reason());
    }
  }
}

static void readCan(uint32_t now) {
  if (!canReady) return;
  uint32_t alerts = 0;
  if (twai_read_alerts(&alerts, 0) == ESP_OK && alerts) {
    djiParser.reset();
    if (activeCan) { canFault("bus/RX alert", ESP_FAIL); return; }
    logLine("Passive CAN alert=0x%08" PRIX32 "\n", alerts);
  }
  for (unsigned i = 0; i < 64; ++i) {
    twai_message_t frame = {};
    const esp_err_t result = twai_receive(&frame, 0);
    if (result == ESP_ERR_TIMEOUT) break;
    if (result != ESP_OK) { canFault("receive", result); break; }
    ++canFrames;
    if (frame.extd || frame.rtr || frame.identifier != dji::RxCanId || frame.data_length_code > 8) continue;
    for (unsigned j = 0; j < frame.data_length_code; ++j) {
      dji::Packet packet;
      if (djiParser.feed(frame.data[j], now, packet) && queryPending &&
          now - querySentMs <= JointTimeoutMs &&
          dji::jointReply(packet, querySequence, joint)) {
        haveJoint = true;
        queryPending = false;
        jointReceivedMs = now;
      }
    }
  }
}

void setup() {
  gpio_set_level(CanTx, 1);
  gpio_set_direction(CanTx, GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(CanTx, GPIO_PULLUP_ONLY);
  pinMode(static_cast<uint8_t>(CanRx), INPUT_PULLUP);
#if !ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MODE
  Serial.setTxBufferSize(2048);
#endif
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  Serial1.setRxBufferSize(1024);
  Serial1.begin(115200, SERIAL_8N1, UwbRx, UnconnectedUartTx);
  Serial1.onReceiveError([](hardwareSerial_error_t error) {
    if (error != UART_NO_ERROR) uartError = true;
  });
  const uint32_t started = millis();
  while (!Serial && millis() - started < 2000) delay(10);
  startCan(false);
  printHelp();
  lastLoopMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if (armed && now - lastLoopMs > 100) disarm("control loop stalled");
  lastLoopMs = now;
  readCan(now);
  readUwb(now);
  readCommands(now);
  requestedSpeed = tracker.step(now);
  if (armed && (!tracker.fresh(now) || !jointFresh(now))) disarm("tag or joint telemetry stale");
  if (armed && fabsf(joint.yaw) >= JointLimitDegrees) disarm("yaw envelope reached");

  if (activeCan) {
    if (queryPending && now - querySentMs > JointTimeoutMs) queryPending = false;
    if (!queryPending && now - querySentMs >= QueryPeriodMs) {
      querySequence = ++packetSequence;
      queryPending = sendPacket(dji::jointQuery(querySequence));
      querySentMs = now;
    }
    if (armed && now - lastControlMs >= ControlPeriodMs) {
      lastControlMs = now;
      float speed = requestedSpeed * direction;
      // Slow before the configured envelope, with margin for telemetry latency.
      const float remaining = JointLimitDegrees - fabsf(joint.yaw);
      if (joint.yaw * speed > 0 && remaining < 10.0f) {
        speed *= fmaxf(0.0f, remaining / 10.0f);
      }
      sendPacket(dji::yawSpeed(++packetSequence, static_cast<int16_t>(lroundf(speed * 10.0f))));
    }
  }
  if (now - lastReportMs >= 500) {
    lastReportMs = now;
    printStatus(now);
  }
  drainConsole();
  delay(1);
}
