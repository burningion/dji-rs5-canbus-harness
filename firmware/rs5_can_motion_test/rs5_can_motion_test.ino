// Finite supervised RS5 bench test, Feather ESP32-S3 8MB/no PSRAM.
// Boots in normal CAN mode without application TX. 'probe' enables angle queries.
// 'run' requires validated fresh joint replies plus a host heartbeat.
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_err.h>
#include <inttypes.h>
#include <stdarg.h>
#include "ConsoleQueue.h"
#include "DjiProtocol.h"
#include "MotionSequence.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select the Adafruit Feather ESP32-S3 No PSRAM board."
#endif

static constexpr gpio_num_t CanTx = GPIO_NUM_5, CanRx = GPIO_NUM_6;
static constexpr uint32_t QueryMs = 100, JointTimeoutMs = 400, HeartbeatMs = 750;
static constexpr uint32_t FaultAlerts = TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF |
    TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_RX_FIFO_OVERRUN | TWAI_ALERT_TX_FAILED;
static bool canReady = false, probing = false, queryPending = false, haveJoint = false;
static uint16_t sequence = 0, querySequence = 0;
static uint32_t querySentMs = 0, jointMs = 0, heartbeatMs = 0, lastControlMs = 0, lastReportMs = 0;
static uint32_t received = 0, validReplies = 0, consecutiveReplies = 0, skippedLogs = 0;
static dji::JointAngles joint;
static dji::StreamParser parser;
static MotionSequence motion;
static ConsoleQueue<4096> console;
static char command[48];
static size_t commandLength = 0;
static bool badCommand = false;

static void logLine(const char *format, ...) {
  char line[256];
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

static void canFault(const char *reason) {
  motion.abort(reason);
  probing = queryPending = haveJoint = false;
  if (canReady) { twai_stop(); twai_driver_uninstall(); }
  canReady = false;
  gpio_set_level(CanTx, 1);
  gpio_set_direction(CanTx, GPIO_MODE_OUTPUT);
  logLine("ABORT CAN fault: %s. TX cleared; no automatic recovery. Use physical power if movement continues.\n", reason);
}

// Complete one SDK packet before sending another. Single-attempt CAN frames
// and bounded completion prevent stale speed requests accumulating in a queue.
static bool sendPacket(const dji::Packet &packet) {
  if (!canReady || !packet.length) return false;
  for (size_t offset = 0; offset < packet.length; offset += 8) {
    twai_message_t frame = {};
    frame.identifier = dji::TxCanId;
    frame.ss = 1;
    const size_t remaining = packet.length - offset;
    frame.data_length_code = remaining > 8 ? 8 : remaining;
    memcpy(frame.data, packet.bytes + offset, frame.data_length_code);
    if (twai_transmit(&frame, pdMS_TO_TICKS(5)) != ESP_OK) {
      canFault("transmit failed"); return false;
    }
  }
  const uint32_t began = millis();
  while (true) {
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) == ESP_OK && (alerts & FaultAlerts)) {
      canFault("bus/TX/RX alert"); return false;
    }
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
  motion.abort(reason);
  bool stopped = true;
  if (wasActive) {
    stopped = sendPacket(dji::yawSpeed(++sequence, 0, true));
    if (stopped) stopped = sendPacket(dji::yawSpeed(++sequence, 0, false));
  }
  if (wasActive) logLine("%s reason=%s zero_and_release_sent=%s\n",
      strcmp(reason, "completed") == 0 ? "DONE" : "ABORT", reason, stopped ? "yes" : "no");
}

static void status(uint32_t now) {
  logLine("BENCH %s phase=%u/12 speed=%.1f joint=%s yaw=%.1f roll=%.1f pitch=%.1f replies=%" PRIu32 " reason=%s\n",
      motion.active() ? "RUNNING" : "IDLE", motion.phase(), motion.speed() / 10.0f,
      fresh(now) ? "fresh" : "stale", joint.yaw, joint.roll, joint.pitch, validReplies, motion.reason());
  twai_status_info_t s = {};
  if (canReady && twai_get_status_info(&s) == ESP_OK)
    logLine("CAN rx=%" PRIu32 " bus_errors=%" PRIu32 " rx_missed=%" PRIu32 " fifo_overrun=%" PRIu32
            " REC=%" PRIu32 " TEC=%" PRIu32 " probing=%s skipped_logs=%" PRIu32 "\n",
        received, s.bus_error_count, s.rx_missed_count, s.rx_overrun_count,
        s.rx_error_counter, s.tx_error_counter, probing ? "yes" : "no", skippedLogs);
}
static void help() {
  logLine("RS5 FINITE MOTION TEST | normal CAN 1Mbit/s | TX=5 RX=6 | no automatic motion\n");
  logLine("probe: query joints; run: 3 cycles of +5deg/s 1s, zero 2s, -5deg/s 1s, zero 2s\n");
  logLine("ping: heartbeat required every <750ms during run; stop: zero/release, stop queries\n");
  logLine("status | help. run needs 3 valid consecutive replies and fresh telemetry.\n");
}
static void handleCommand(const char *line, uint32_t now) {
  if (!strcmp(line, "ping")) { heartbeatMs = now; return; }
  if (!strcmp(line, "help")) { help(); return; }
  if (!strcmp(line, "status")) { status(now); return; }
  if (!strcmp(line, "stop")) {
    stopMotion("operator stop");
    probing = queryPending = false;
    logLine("STOPPED; no application commands pending.\n"); return;
  }
  if (!strcmp(line, "probe")) {
    stopMotion("probe requested");
    if (!canReady) { logLine("Cannot probe: reset after fixing CAN fault.\n"); return; }
    probing = true; haveJoint = queryPending = false; consecutiveReplies = 0;
    parser.reset(); querySentMs = now - QueryMs;
    logLine("PROBE: joint-angle queries only.\n"); return;
  }
  if (!strcmp(line, "run")) {
    if (!canReady || !probing || !fresh(now) || consecutiveReplies < 3 ||
        !Serial || now - heartbeatMs > 300 || motion.active()) {
      logLine("RUN REFUSED: require healthy CAN, fresh joint replies, ping, and idle state.\n"); return;
    }
    if (!motion.start(now, joint.yaw, joint.roll, joint.pitch)) {
      logLine("RUN REFUSED: starting joint yaw must be within +/-70 degrees.\n"); return;
    }
    lastControlMs = now - 50;
    logLine("RUN START yaw=%.1f: three cycles, +/-5deg/s; stops at completion or fault.\n", joint.yaw);
    return;
  }
  if (motion.active()) stopMotion("invalid console command");
  logLine("Unknown command; use help.\n");
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
  if (twai_read_alerts(&alerts, 0) == ESP_OK && (alerts & FaultAlerts)) {
    canFault("bus/RX alert"); return;
  }
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
  gpio_set_level(CanTx, 1); gpio_set_direction(CanTx, GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(CanTx, GPIO_PULLUP_ONLY);
  pinMode(static_cast<uint8_t>(CanRx), INPUT_PULLUP);
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  twai_general_config_t config = TWAI_GENERAL_CONFIG_DEFAULT(CanTx, CanRx, TWAI_MODE_NORMAL);
  config.tx_queue_len = 8; config.rx_queue_len = 256; config.alerts_enabled = FaultAlerts;
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
  readCommands();
  uint32_t now = millis();
  const bool wasActive = motion.active();
  const unsigned previousPhase = motion.phase();
  motion.tick(now, joint.yaw, joint.roll, joint.pitch, fresh(now), Serial && now - heartbeatMs <= HeartbeatMs);
  if (wasActive && !motion.active()) stopMotion(motion.reason(), true);
  if (motion.active() && previousPhase != motion.phase()) {
    logLine("PHASE %u cycle=%u speed=%.1f yaw=%.1f\n", motion.phase(), motion.phase()/4+1, motion.speed()/10.0f, joint.yaw);
    lastControlMs = now - 50;
  }
  if (canReady && motion.active() && now - lastControlMs >= 50) {
    lastControlMs = now;
    sendPacket(dji::yawSpeed(++sequence, motion.speed(), true));
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
  if (now - lastReportMs >= 250) { status(now); lastReportMs = now; }
  drainConsole();
  delay(1);
}
