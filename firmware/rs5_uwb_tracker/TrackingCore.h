#pragma once

#include <math.h>
#include <stdint.h>
#include "MakerfabsProtocol.h"

namespace tracking {
struct Config {
  float zeroDegrees = 0.0f;       // Measured bearing with the tag at lens center.
  float deadbandDegrees = 3.0f;
  float gain = 1.0f;              // deg/s per degree of error outside deadband.
  float maxSpeed = 15.0f;         // Conservative bench defaults, not tuned on RS5.
  float acceleration = 45.0f;    // deg/s^2.
  float filterSeconds = 0.25f;
  float maxBearing = 55.0f;       // Inside the vendor's nominal +/-60 degree FoV.
  float maxJump = 25.0f;
  int32_t minDistanceCm = 75;
  int32_t maxDistanceCm = 2000;
  uint32_t staleMs = 300;
  unsigned acquireSamples = 3;
};

class Tracker {
 public:
  explicit Tracker(const Config &config = Config()) : config_(config) {}
  void select(uint16_t tag) { tag_ = tag; selected_ = true; reset(); }
  // A newly armed controller must accelerate from rest, even after previewing.
  void restartOutput(uint32_t now) { speed_ = 0; lastStepMs_ = now; }
  void reset() {
    acquired_ = 0;
    haveSample_ = haveSequence_ = false;
    speed_ = 0;
    reason_ = "waiting for tag";
  }
  void invalidate(const char *reason) {
    acquired_ = 0;
    haveSample_ = false;
    speed_ = 0;
    reason_ = reason;
    // Retain sequence history: replaying an old report cannot refresh the timeout.
  }
  bool update(const uwb::Measurement &m, uint32_t now) {
    if (!selected_ || m.tag != tag_) return false;
    if (haveSequence_) {
      const uint8_t delta = static_cast<uint8_t>(m.sequence - lastSequence_);
      if (delta == 0) return false;
      if (now - sequenceTime_ <= config_.staleMs && delta >= 128) return false;
    }
    lastSequence_ = m.sequence;
    sequenceTime_ = now;
    haveSequence_ = true;
    if (m.distanceCm < config_.minDistanceCm || m.distanceCm > config_.maxDistanceCm ||
        m.yCm <= 0 || absDouble(m.xCm) > 3000 || m.yCm > 3000) {
      invalidate("range/coordinates invalid");
      return false;
    }
    const float bearing = atan2f(static_cast<float>(m.xCm), static_cast<float>(m.yCm)) *
                          (180.0f / 3.14159265358979323846f);
    if (!isfinite(bearing) || fabsf(bearing) > config_.maxBearing) {
      invalidate("outside tracking field");
      return false;
    }
    const bool continuing = haveSample_ && now - lastSampleMs_ <= config_.staleMs;
    if (continuing && fabsf(bearing - lastBearing_) > config_.maxJump) {
      invalidate("bearing jump");
      return false;
    }
    if (continuing) {
      const float dt = static_cast<float>(now - lastSampleMs_) / 1000.0f;
      const float alpha = dt / (config_.filterSeconds + dt);
      filtered_ += alpha * (bearing - filtered_);
    } else {
      filtered_ = bearing;
      acquired_ = 0;
      speed_ = 0;
    }
    if (acquired_ < config_.acquireSamples) ++acquired_;
    lastBearing_ = bearing;
    lastSampleMs_ = now;
    distanceCm_ = m.distanceCm;
    haveSample_ = true;
    reason_ = acquired_ >= config_.acquireSamples ? "tracking" : "acquiring";
    return true;
  }
  bool fresh(uint32_t now) const {
    return selected_ && haveSample_ && acquired_ >= config_.acquireSamples &&
           now - lastSampleMs_ <= config_.staleMs;
  }
  float step(uint32_t now) {
    float dt = static_cast<float>(now - lastStepMs_) / 1000.0f;
    lastStepMs_ = now;
    if (!fresh(now)) {
      speed_ = 0;
      if (haveSample_ && now - lastSampleMs_ > config_.staleMs) reason_ = "tag stale";
      return 0;
    }
    if (dt > 0.1f) dt = 0.1f;
    const float error = filtered_ - config_.zeroDegrees;
    const float magnitude = fmaxf(0.0f, fabsf(error) - config_.deadbandDegrees);
    const float target = copysignf(fminf(config_.maxSpeed, config_.gain * magnitude), error);
    const float change = config_.acceleration * dt;
    speed_ += fmaxf(-change, fminf(change, target - speed_));
    return speed_;
  }
  const char *reason() const { return selected_ ? reason_ : "select a tag"; }
  uint16_t tag() const { return tag_; }
  bool selected() const { return selected_; }
  float bearing() const { return filtered_; }
  int32_t distanceCm() const { return distanceCm_; }

 private:
  static double absDouble(int32_t value) { return fabs(static_cast<double>(value)); }
  Config config_;
  bool selected_ = false, haveSample_ = false, haveSequence_ = false;
  uint16_t tag_ = 0;
  uint8_t lastSequence_ = 0;
  unsigned acquired_ = 0;
  uint32_t lastSampleMs_ = 0, sequenceTime_ = 0, lastStepMs_ = 0;
  float filtered_ = 0, lastBearing_ = 0, speed_ = 0;
  int32_t distanceCm_ = 0;
  const char *reason_ = "select a tag";
};
}  // namespace tracking
