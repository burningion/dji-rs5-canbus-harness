#pragma once
#include <math.h>
#include <stdint.h>

// Independent device-side speed cap and input/telemetry watchdogs.
class ManualControl {
 public:
  static constexpr uint32_t InputTimeoutMs = 300;
  static constexpr int MaxSpeed = 600;  // tenths of a degree / second, per axis
  bool arm(uint32_t now, float yaw, float roll, float pitch) {
    if (active_ || !finite(yaw, roll, pitch)) return false;
    lastTick_ = lastInput_ = now; lastSequence_ = 0;
    yaw_ = pitch_ = 0; active_ = true; reason_ = "enabled";
    return true;
  }
  bool input(uint32_t now, uint32_t sequence, int yaw, int pitch) {
    if (!active_) return false;
    // Expire BEFORE accepting a late input, even if the main loop was stalled.
    if (now - lastInput_ > InputTimeoutMs) { stop("input_timeout"); return false; }
    if (!sequence || sequence <= lastSequence_ || yaw < -MaxSpeed || yaw > MaxSpeed ||
        pitch < -MaxSpeed || pitch > MaxSpeed) { stop("invalid_input"); return false; }
    lastSequence_ = sequence; lastInput_ = now; yaw_ = yaw; pitch_ = pitch;
    return true;
  }
  void tick(uint32_t now, float yaw, float roll, float pitch, bool fresh, bool connected) {
    if (!active_) return;
    if (now - lastTick_ > 100) stop("loop_stall");
    else if (!connected) stop("usb_disconnected");
    else if (!fresh) stop("telemetry_stale");
    else if (now - lastInput_ > InputTimeoutMs) stop("input_timeout");
    else if (!finite(yaw, roll, pitch)) stop("invalid_angles");
    lastTick_ = now;
  }
  void stop(const char *reason) { active_ = false; yaw_ = pitch_ = 0; reason_ = reason; }
  bool active() const { return active_; }
  int16_t yaw() const { return yaw_; }
  int16_t pitch() const { return pitch_; }
  const char *reason() const { return reason_; }
 private:
  static bool finite(float a, float b, float c) { return isfinite(a) && isfinite(b) && isfinite(c); }
  bool active_ = false;
  int16_t yaw_ = 0, pitch_ = 0;
  uint32_t lastInput_ = 0, lastTick_ = 0, lastSequence_ = 0;
  const char *reason_ = "idle";
};
