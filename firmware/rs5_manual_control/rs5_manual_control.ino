// Local UI manual control. Boots disarmed; no automatic movement.
// Firmware leases and telemetry guards remain authoritative if the host fails.
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_err.h>
#include <inttypes.h>
#include <stdarg.h>
#include <esp_random.h>
#include <stdlib.h>
#include <errno.h>
#include "ConsoleQueue.h"
#include "DjiProtocol.h"
#include "ManualControl.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select the Adafruit Feather ESP32-S3 No PSRAM board."
#endif

static constexpr gpio_num_t CanTx = GPIO_NUM_5, CanRx = GPIO_NUM_6;
static constexpr uint32_t QueryMs = 100, JointTimeoutMs = 400;
static constexpr uint32_t FaultAlerts = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF |
    TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN | TWAI_ALERT_TX_FAILED;
static bool canReady = false, probing = false, queryPending = false, haveJoint = false;
static uint16_t sequence = 0, querySequence = 0;
static uint32_t querySentMs = 0, jointMs = 0, lastControlMs = 0, lastReportMs = 0;
static uint32_t received = 0, validReplies = 0, consecutiveReplies = 0, skippedLogs = 0;
static dji::JointAngles joint;
static dji::StreamParser parser;
static ManualControl motion;
static uint32_t sessionToken = 0;
static twai_status_info_t lastCanStatus = {};
static uint32_t faultAlerts = 0;
static const char *faultReason = "none";
static ConsoleQueue<4096> console;
static char command[128];
static size_t commandLength = 0;
static bool badCommand = false;

static void logLine(const char *format, ...) {
  char line[768];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length <= 0 || length >= static_cast<int>(sizeof(line)) || !Serial ||
      !console.append(line, static_cast<size_t>(length))) ++skippedLogs;
}
static void drainConsole() {
  if (!Serial || !console.size()) return;
  const int available = Serial.availableForWrite();
  if (available <= 0) return;
  size_t n = console.contiguousSize();
  if (n > static_cast<size_t>(available)) n = available;
  console.consume(Serial.write(console.data(), n));
}
static bool fresh(uint32_t now) { return haveJoint && now - jointMs <= JointTimeoutMs; }

static void canFault(const char *reason, uint32_t alerts = 0) {
  faultReason = reason;
  faultAlerts = alerts;
  if (canReady) twai_get_status_info(&lastCanStatus);
  motion.stop(reason);
  sessionToken = esp_random();
  probing = queryPending = haveJoint = false;
  if (canReady) { twai_stop(); twai_driver_uninstall(); }
  canReady = false;
  gpio_set_level(CanTx, 1);
  gpio_set_direction(CanTx, GPIO_MODE_OUTPUT);
  logLine("ABORT CAN fault: %s alerts=0x%08" PRIx32 " tx_failed=%" PRIu32
          " arbitration_lost=%" PRIu32 " bus_errors=%" PRIu32
          ". TX cleared; reset required.\n", reason, alerts, lastCanStatus.tx_failed_count,
          lastCanStatus.arb_lost_count, lastCanStatus.bus_error_count);
}

static bool checkAlerts(uint32_t alerts) {
  if (!(alerts & FaultAlerts)) return true; // Arbitration loss alone is normal CAN.
  const char *reason = alerts & TWAI_ALERT_BUS_OFF ? "can_bus_off" :
      alerts & TWAI_ALERT_TX_FAILED ? "can_transmit_failed" :
      alerts & TWAI_ALERT_BUS_ERROR ? "can_bus_error" :
      alerts & TWAI_ALERT_RX_QUEUE_FULL ? "can_receive_queue_full" : "can_receive_overrun";
  canFault(reason, alerts);
  return false;
}

// Complete one SDK packet before another. Normal CAN arbitration retries are
// allowed within a 20ms packet deadline; expiry stops the driver and clears TX.
static bool sendPacket(const dji::Packet &packet) {
  if (!canReady || !packet.length) return false;
  const uint32_t began = millis();
  for (size_t offset = 0; offset < packet.length; offset += 8) {
    if (millis() - began >= 20) { canFault("transmit completion timeout"); return false; }
    twai_message_t frame = {};
    frame.identifier = dji::TxCanId;
    frame.ss = 0; // Single-shot would also fail on ordinary arbitration loss.
    const size_t remaining = packet.length - offset;
    frame.data_length_code = remaining > 8 ? 8 : remaining;
    memcpy(frame.data, packet.bytes + offset, frame.data_length_code);
    if (twai_transmit(&frame, 0) != ESP_OK) {
      canFault("transmit failed"); return false;
    }
  }
  while (true) {
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) == ESP_OK && !checkAlerts(alerts)) return false;
    twai_status_info_t status = {};
    if (twai_get_status_info(&status) != ESP_OK || status.state != TWAI_STATE_RUNNING) {
      canFault("driver state"); return false;
    }
    if (status.msgs_to_tx == 0) return true;
    if (millis() - began >= 20) { canFault("transmit completion timeout"); return false; }
    delay(1);
  }
}

static void stopMotion(const char *reason, bool forceStop = false) {
  const bool wasActive = motion.active() || forceStop;
  motion.stop(reason);
  sessionToken = esp_random();
  bool stopped = true;
  if (wasActive) {
    stopped = sendPacket(dji::axisSpeed(++sequence, 0, 0, true));
    if (stopped) stopped = sendPacket(dji::axisSpeed(++sequence, 0, 0, false));
  }
  if (wasActive) logLine("%s reason=%s zero_and_release_sent=%s\n",
      strcmp(reason, "completed") == 0 ? "DONE" : "ABORT", reason, stopped ? "yes" : "no");
}

static void status(uint32_t now) {
  if (canReady) twai_get_status_info(&lastCanStatus);
  const twai_status_info_t &s = lastCanStatus;
  logLine("{\"type\":\"device\",\"protocol\":2,\"max_speed_dps\":%u,\"clock\":%" PRIu32
          ",\"token\":%" PRIu32 ",\"armed\":%s,\"can_ready\":%s,\"fresh\":%s,"
          "\"ready\":%s,\"probing\":%s,\"yaw\":%.1f,\"roll\":%.1f,\"pitch\":%.1f,"
          "\"pan_speed\":%.1f,\"tilt_speed\":%.1f,\"replies\":%" PRIu32 ",\"reason\":\"%s\","
          "\"bus_errors\":%" PRIu32 ",\"rx_missed\":%" PRIu32 ",\"fifo_overrun\":%" PRIu32
          ",\"rec\":%" PRIu32 ",\"tec\":%" PRIu32 ",\"tx_failed\":%" PRIu32
          ",\"arbitration_lost\":%" PRIu32 ",\"fault_alerts\":%" PRIu32 ",\"bounded_retry\":true}\n",
      static_cast<unsigned>(ManualControl::MaxSpeed / 10), now, sessionToken, motion.active() ? "true" : "false", canReady ? "true" : "false",
      fresh(now) ? "true" : "false", canReady && probing && fresh(now) && consecutiveReplies >= 3 ? "true" : "false",
      probing ? "true" : "false", joint.yaw, joint.roll, joint.pitch,
      motion.yaw()/10.0f, motion.pitch()/10.0f, validReplies, canReady ? motion.reason() : faultReason,
      s.bus_error_count, s.rx_missed_count, s.rx_overrun_count, s.rx_error_counter, s.tx_error_counter,
      s.tx_failed_count, s.arb_lost_count, faultAlerts);
}
static void help() {
  logLine("RS5 MANUAL CONTROL protocol=2 | CAN 1Mbit/s | TX=5 RX=6 | boots disarmed\n");
  logLine("probe | stop | idle | status | arm TOKEN CLOCK | drive TOKEN CLOCK SEQ PAN TILT\n");
}
static bool unsignedValue(const char *text, uint32_t &out) {
  if (!text || !*text) return false;
  for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
  errno = 0; char *end = nullptr;
  const unsigned long value = strtoul(text, &end, 10);
  if (errno || *end || value > UINT32_MAX) return false;
  out = static_cast<uint32_t>(value); return true;
}
static bool speedValue(const char *text, int &out) {
  if (!text || !*text) return false;
  errno = 0; char *end = nullptr;
  const long value = strtol(text, &end, 10);
  if (errno || *end || value < -ManualControl::MaxSpeed || value > ManualControl::MaxSpeed) return false;
  out = static_cast<int>(value); return true;
}
static void handleCommand(char *line, uint32_t now) {
  if (!strcmp(line, "help")) { help(); return; }
  if (!strcmp(line, "status")) { status(now); return; }
  if (!strcmp(line, "stop") || !strcmp(line, "idle")) {
    stopMotion("operator_stop");
    if (!strcmp(line, "idle")) probing = queryPending = false;
    status(millis()); return;
  }
  if (!strcmp(line, "probe")) {
    stopMotion("probing");
    if (!canReady) { logLine("REFUSED reset required after CAN fault\n"); return; }
    probing = true; haveJoint = queryPending = false; consecutiveReplies = 0;
    parser.reset(); querySentMs = now - QueryMs;
    return;
  }
  char *args[7] = {}; unsigned count = 0; char *save = nullptr;
  for (char *p = strtok_r(line, " ", &save); p; p = strtok_r(nullptr, " ", &save)) {
    if (count == 7) { stopMotion("invalid_command"); return; }
    args[count++] = p;
  }
  uint32_t token = 0, stamp = 0, number = 0;
  if (count < 3 || !unsignedValue(args[1], token) || !unsignedValue(args[2], stamp) ||
      token != sessionToken || now - stamp > 300) {
    stopMotion("stale_command"); return;
  }
  if (count == 3 && !strcmp(args[0], "arm")) {
    if (!canReady || !probing || !fresh(now) || consecutiveReplies < 3 || !Serial ||
        !motion.arm(now, joint.yaw, joint.roll, joint.pitch)) {
      stopMotion("enable_refused"); status(millis()); return;
    }
    lastControlMs = now - 50;
    status(now); return;
  }
  int pan = 0, tilt = 0;
  if (count == 6 && !strcmp(args[0], "drive") && unsignedValue(args[3], number) &&
      speedValue(args[4], pan) && speedValue(args[5], tilt)) {
    const bool wasActive = motion.active();
    if (!motion.input(now, number, pan, tilt)) {
      if (wasActive) stopMotion(motion.reason(), true);
      return;
    }
    // Zero input stops on this iteration, without waiting for the next 50ms tick.
    if (pan == 0 && tilt == 0) lastControlMs = now - 50;
    return;
  }
  stopMotion("invalid_command");
}
static void readCommands() {
  for (unsigned i = 0; i < 128 && Serial.available(); ++i) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (!badCommand && commandLength) {
        command[commandLength] = 0;
        handleCommand(command, millis());
      }
      commandLength = 0; badCommand = false;
    } else if (!badCommand) {
      if (c < 32 || c > 126 || commandLength + 1 >= sizeof(command)) {
        badCommand = true; stopMotion("invalid console input");
      } else command[commandLength++] = c;
    }
  }
}
static void readCan() {
  if (!canReady) return;
  uint32_t alerts = 0;
  if (twai_read_alerts(&alerts, 0) == ESP_OK && !checkAlerts(alerts)) return;
  for (unsigned i = 0; i < 128; ++i) {
    twai_message_t frame = {};
    const esp_err_t result = twai_receive(&frame, 0);
    if (result == ESP_ERR_TIMEOUT) break;
    if (result != ESP_OK) { canFault("receive failed"); return; }
    ++received;
    if (frame.extd || frame.rtr || frame.identifier != dji::RxCanId || frame.data_length_code > 8) continue;
    const uint32_t now = millis();
    for (unsigned b = 0; b < frame.data_length_code; ++b) {
      dji::Packet packet;
      if (parser.feed(frame.data[b], now, packet) && queryPending && now - querySentMs <= JointTimeoutMs &&
          dji::jointReply(packet, querySequence, joint)) {
        haveJoint = true; queryPending = false; jointMs = now;
        ++validReplies; if (consecutiveReplies < 3) ++consecutiveReplies;
      }
    }
  }
}
void setup() {
  sessionToken = esp_random();
  gpio_set_level(CanTx, 1); gpio_set_direction(CanTx, GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(CanTx, GPIO_PULLUP_ONLY);
  pinMode(static_cast<uint8_t>(CanRx), INPUT_PULLUP);
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(CanTx, CanRx, TWAI_MODE_NORMAL);
  config.tx_queue_len = 8; config.rx_queue_len = 256;
  config.alerts_enabled = FaultAlerts | TWAI_ALERT_ARB_LOST;
  const twai_timing_config_t timing = TWAI_TIMING_CONFIG_1MBITS();
  const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&config, &timing, &filter) == ESP_OK) {
    canReady = true;
    if (twai_start() != ESP_OK) canFault("start failed");
  } else canFault("install failed");
  help();
}
void loop() {
  readCan();
  // Evaluate expiry before draining serial so queued input cannot revive a lease.
  uint32_t now = millis();
  bool wasActive = motion.active();
  motion.tick(now, joint.yaw, joint.roll, joint.pitch, fresh(now), !!Serial);
  if (wasActive && !motion.active()) stopMotion(motion.reason(), true);
  if (!Serial) probing = queryPending = false;
  readCommands();
  now = millis();
  wasActive = motion.active();
  motion.tick(now, joint.yaw, joint.roll, joint.pitch, fresh(now), !!Serial);
  if (wasActive && !motion.active()) stopMotion(motion.reason(), true);
  if (canReady && motion.active() && now - lastControlMs >= 50) {
    lastControlMs = now;
    sendPacket(dji::axisSpeed(++sequence, motion.yaw(), motion.pitch(), true));
  }
  now = millis();
  if (canReady && probing) {
    if (queryPending && now - querySentMs > JointTimeoutMs) {
      queryPending = false; consecutiveReplies = 0; parser.reset();
    }
    if (!queryPending && now - querySentMs >= QueryMs) {
      querySequence = ++sequence; querySentMs = now;
      queryPending = sendPacket(dji::jointQuery(querySequence));
    }
  }
  now = millis();
  if (now - lastReportMs >= 50) { status(now); lastReportMs = now; }
  drainConsole();
  delay(1);
}
