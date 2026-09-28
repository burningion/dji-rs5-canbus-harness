#pragma once
#include <math.h>
#include <stdint.h>

// A finite bench test. No automatic startup/restart, and no user-set speed.
class MotionSequence {
 public:
  static constexpr unsigned Cycles = 3;
  static constexpr int16_t SpeedTenths = 50;
  static constexpr uint32_t MoveMs = 1000, PauseMs = 2000;

  bool start(uint32_t now, float yaw, float roll, float pitch) {
    if (active_ || !isfinite(yaw) || !isfinite(roll) || !isfinite(pitch) || fabsf(yaw) > 70) return false;
    originYaw_ = yaw; originRoll_ = roll; originPitch_ = pitch;
    phase_ = 0; phaseStarted_ = lastTick_ = now;
    active_ = true; reason_ = "running";
    return true;
  }
  void abort(const char *reason) { active_ = false; reason_ = reason; }
  void tick(uint32_t now, float yaw, float roll, float pitch, bool telemetryFresh, bool hostAlive) {
    if (!active_) return;
    if (now - lastTick_ > 100) { abort("loop stalled"); return; }
    lastTick_ = now;
    if (!telemetryFresh) { abort("telemetry stale"); return; }
    if (!hostAlive) { abort("host heartbeat lost"); return; }
    if (!isfinite(yaw) || !isfinite(roll) || !isfinite(pitch) ||
        fabsf(yaw - originYaw_) > 8 || fabsf(yaw) > 80 ||
        fabsf(roll - originRoll_) > 5 || fabsf(pitch - originPitch_) > 5) {
      abort("angle envelope exceeded"); return;
    }
    const uint32_t duration = phase_ % 2 == 0 ? MoveMs : PauseMs;
    if (now - phaseStarted_ >= duration) {
      ++phase_; phaseStarted_ = now;
      if (phase_ == Cycles * 4) abort("completed");
    }
  }
  bool active() const { return active_; }
  unsigned phase() const { return phase_; }
  const char *reason() const { return reason_; }
  int16_t speed() const {
    if (!active_) return 0;
    return phase_ % 4 == 0 ? SpeedTenths : (phase_ % 4 == 2 ? -SpeedTenths : 0);
  }
 private:
  bool active_ = false;
  unsigned phase_ = 0;
  uint32_t phaseStarted_ = 0, lastTick_ = 0;
  float originYaw_ = 0, originRoll_ = 0, originPitch_ = 0;
  const char *reason_ = "idle";
};
