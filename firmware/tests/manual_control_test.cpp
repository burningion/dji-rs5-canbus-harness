#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../rs5_manual_control/ManualControl.h"
#include "../rs5_manual_control/DjiProtocol.h"

static void watchdogs() {
  ManualControl m;
  assert(!m.active() && !m.input(0, 1, 10, 0));
  assert(!m.arm(0, NAN, 0, 0) && !m.arm(0, 0, INFINITY, 0) && !m.arm(0, 0, 0, NAN));
  assert(m.arm(0, 2, 1, 0));
  assert(m.yaw() == 0 && m.pitch() == 0);
  assert(!m.arm(1, 2, 1, 0));
  assert(m.input(10, 1, 600, -600));
  assert(m.yaw() == 600 && m.pitch() == -600);
  assert(!m.input(20, 1, 600, 600)); // Replayed sequence disarms.
  assert(!m.active() && m.yaw() == 0 && m.pitch() == 0);
  assert(m.arm(100, 0, 0, 0));
  assert(!m.input(101, 1, 601, 0) && !m.active());
  assert(m.arm(200, 0, 0, 0));
  assert(!m.input(201, 1, 0, -601) && !m.active());
  assert(m.arm(300, 0, 0, 0));
  assert(!m.input(601, 1, 1, 1)); // Late input cannot revive expired lease.
  assert(!m.active());
  assert(m.arm(700, 0, 0, 0));
  for (unsigned t = 750; t <= 1000; t += 50) m.tick(t, 0, 0, 0, true, true);
  assert(m.active());
  m.tick(1001, 0, 0, 0, true, true);
  assert(!m.active());
  assert(m.arm(2000, 0, 0, 0));
  m.tick(2101, 0, 0, 0, true, true); assert(!m.active());
  assert(m.arm(3000, 0, 0, 0));
  m.tick(3010, 0, 0, 0, false, true); assert(!m.active());
  assert(m.arm(4000, 0, 0, 0));
  m.tick(4010, 0, 0, 0, true, false); assert(!m.active());
}
static void continuousTravel() {
  ManualControl travel;
  assert(travel.arm(0, 179, 240, 214)); // Old initial-angle limits do not apply.
  // Three complete pan revolutions at 60deg/s. Telemetry wraps through +/-180;
  // commands stay constant, with no angle delta or per-enable excursion limit.
  for (uint32_t n = 1; n <= 360; ++n) {
    const uint32_t now = n * 50;
    const float yaw = fmodf(179 + n * 3 + 180, 360) - 180;
    assert(travel.input(now, n, 600, -600));
    travel.tick(now, yaw, 200, 190, true, true);
    assert(travel.active() && travel.yaw() == 600 && travel.pitch() == -600);
  }
  travel.stop("operator_stop");
  assert(travel.arm(18100, -179, -95, -112));
  assert(travel.input(18150, 1, -600, 600));
  travel.tick(18150, 178, -95, -112, true, true); // Reverse seam crossing.
  assert(travel.active() && travel.yaw() == -600);
  assert(travel.input(18200, 2, 0, 0));
  assert(!travel.yaw() && !travel.pitch()); // Release remains immediate.
  for (unsigned fault = 0; fault < 3; ++fault) {
    ManualControl m;
    assert(m.arm(0, 0, 0, 0));
    assert(m.input(1, 1, 40, -25));
    m.tick(50, fault == 0 ? NAN : 0, fault == 1 ? INFINITY : 0, fault == 2 ? NAN : 0, true, true);
    assert(!m.active() && !m.yaw() && !m.pitch());
    assert(!m.input(60, 2, 0, 0)); // No automatic restart.
  }
  ManualControl m;
  assert(m.arm(0xffffffc0, 0, 0, 0));
  assert(m.input(0xfffffff0, 1, 50, 25));
  m.tick(0x10, 1, 0, 1, true, true); assert(m.active());
  m.stop("operator_stop"); assert(!m.active() && !m.yaw() && !m.pitch());
}
static void protocol() {
  auto p = dji::axisSpeed(0x1234, 600, -600);
  assert(p.length == 25 && p.bytes[12] == 0x0e && p.bytes[13] == 0x01);
  assert(p.bytes[14] == 0x58 && p.bytes[15] == 0x02); // yaw +60.0
  assert(p.bytes[16] == 0 && p.bytes[17] == 0); // roll fixed
  assert(p.bytes[18] == 0xa8 && p.bytes[19] == 0xfd); // pitch -60.0
  assert(p.bytes[20] == 0x88);
  auto stop = dji::axisSpeed(0x1235, 0, 0, false);
  for (unsigned i = 14; i < 20; ++i) assert(stop.bytes[i] == 0);
  assert(stop.bytes[20] == 0x08);
  // Independent known-answer vector from DJI SDK v2.5 section 3.3.
  const uint8_t expected[] = {0xaa,0x1a,0,3,0,0,0,0,0x22,0x11,0xa2,0x42,0x0e,0,
    0x20,0,0x30,0,0x40,0,1,0x14,0x7b,0x40,0x97,0xbe};
  const auto generated = dji::command(0x1122, 3, 0x0e, 0, expected + 14, 8);
  assert(generated.length == sizeof(expected));
  assert(memcmp(generated.bytes, expected, sizeof(expected)) == 0);
  dji::StreamParser parser; dji::Packet decoded; bool ready = false;
  for (unsigned i = 0; i < p.length; ++i) ready |= parser.feed(p.bytes[i], 0, decoded);
  assert(ready && decoded.length == p.length);

  // Joint feedback outside +/-180 is defined by the signed wire field, not a
  // software travel policy; keep validating framing and query sequence.
  uint8_t data[] = {0,2,0,0,0,0,0,0};
  const int16_t yawSamples[] = {1799,1800,-1800,-1799,3599,32767,-32768};
  for (int16_t yaw : yawSamples) {
    dji::write16(data + 2, static_cast<uint16_t>(yaw));
    dji::write16(data + 4, 2400);
    dji::write16(data + 6, 2140);
    auto reply = dji::command(9, 0x20, 0x0e, 2, data, sizeof(data));
    parser.reset(); ready = false;
    for (unsigned i = 0; i < reply.length; ++i) ready |= parser.feed(reply.bytes[i], 10, decoded);
    dji::JointAngles angles;
    assert(ready && dji::jointReply(decoded, 9, angles));
    assert(angles.yaw == yaw / 10.0f && angles.roll == 240 && angles.pitch == 214);
    assert(!dji::jointReply(decoded, 10, angles));
    reply.bytes[18] ^= 1; // A corrupted angle still fails CRC validation.
    parser.reset(); ready = false;
    for (unsigned i = 0; i < reply.length; ++i) ready |= parser.feed(reply.bytes[i], 10, decoded);
    assert(!ready);
  }
}
int main() { watchdogs(); continuousTravel(); protocol(); puts("60deg/s, continuous travel/wrap, watchdogs, replay rejection and SDK encoding passed."); }
