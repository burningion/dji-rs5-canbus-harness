#include "../rs5_can_motion_test/MotionSequence.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

static void complete(uint32_t start) {
  MotionSequence s;
  assert(!s.active() && s.speed() == 0);
  assert(s.start(start, 2, 0, 0));
  assert(!s.start(start, 2, 0, 0));
  float yaw = 2;
  unsigned changes = 0, previous = 0;
  for (uint32_t elapsed = 10; elapsed <= 18000; elapsed += 10) {
    yaw += s.speed() / 10.0f * .01f;
    s.tick(start + elapsed, yaw, 0, 0, true, true);
    if (s.phase() != previous) { ++changes; previous = s.phase(); }
    const unsigned cycleTime = elapsed % 6000;
    if (elapsed < 18000) {
      const int expected = cycleTime < 1000 ? 50 : (cycleTime >= 3000 && cycleTime < 4000 ? -50 : 0);
      assert(s.active() && s.speed() == expected);
    }
  }
  assert(changes == 12 && !s.active() && s.speed() == 0);
  assert(std::strcmp(s.reason(), "completed") == 0 && fabsf(yaw - 2) < .01);
  s.tick(start + 18010, 2, 0, 0, true, true);
  assert(!s.active());
}

int main() {
  complete(1000);
  complete(0xfffff000U);  // Time arithmetic across millis() wrap.
  for (unsigned failure = 0; failure < 7; ++failure) {
    MotionSequence s;
    assert(s.start(100, 2, 0, 0));
    s.tick(failure == 0 ? 201 : 150,
           failure == 3 ? 10.1f : failure == 6 ? std::numeric_limits<float>::quiet_NaN() : 2,
           failure == 4 ? 5.1f : 0, failure == 5 ? -5.1f : 0,
           failure != 1, failure != 2);
    assert(!s.active() && s.speed() == 0);
    s.tick(220, 2, 0, 0, true, true);
    assert(!s.active());  // Fault recovery never automatically restarts motion.
  }
  MotionSequence s;
  assert(!s.start(0, 71, 0, 0));
  assert(s.start(0, 0, 0, 0));
  s.abort("operator stop");
  assert(!s.active() && s.speed() == 0);
  std::cout << "Motion sequence: three bounded cycles, pauses, wrap, stop and fault gates passed.\n";
}
