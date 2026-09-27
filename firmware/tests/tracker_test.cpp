#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "../rs5_uwb_tracker/MakerfabsProtocol.h"
#include "../rs5_uwb_tracker/TrackingCore.h"
#include "../rs5_uwb_tracker/DjiProtocol.h"

static std::string frame(const std::string &json) {
  char header[7];
  snprintf(header, sizeof(header), "JS%04X", static_cast<unsigned>(json.size()));
  return std::string(header) + json + "\r\n";
}

static uwb::ParseResult parse(const std::string &bytes, uwb::Measurement &m) {
  uwb::StreamParser parser;
  uwb::ParseResult result = uwb::ParseResult::None;
  for (uint8_t byte : bytes) {
    const auto next = parser.feed(byte, 100, m);
    if (next != uwb::ParseResult::None) result = next;
  }
  return result;
}

static uwb::Measurement measurement(float degrees, uint8_t sequence, uint16_t tag = 0x2e5c) {
  uwb::Measurement m;
  m.tag = tag;
  m.sequence = sequence;
  m.distanceCm = 500;
  m.xCm = lroundf(sinf(degrees * 3.14159265f / 180) * 500);
  m.yCm = lroundf(cosf(degrees * 3.14159265f / 180) * 500);
  return m;
}

static void testUwb() {
  // The field order and values follow the stock Makerfabs json_2pc.c emitter.
  const std::string json = R"({"TWR": {"a16":"2E5C","R":3,"T":8605,"D":343,"P":1695,"Xcm":165,"Ycm":165,"O":14,"V":1,"X":53015,"Y":60972,"Z":10797}})";
  uwb::Measurement m;
  assert(parse("boot noise\r\n" + frame(json), m) == uwb::ParseResult::Measurement);
  assert(m.tag == 0x2e5c && m.sequence == 3 && m.distanceCm == 343);
  assert(m.xCm == 165 && m.yCm == 165);  // Not raw phase P or acceleration X/Y.
  assert(parse(frame(R"({"NewTag":"1122334455667788"})"), m) == uwb::ParseResult::None);

  const std::vector<std::string> invalid = {
    R"({"TWR":{"a16":"1234","R":1,"D":500,"Xcm":1}})",  // Missing Ycm.
    R"({"TWR":{"a16":"1234","R":1,"D":500,"Xcm":1,"Ycm":2,"R":2}})",
    R"({"TWR":{"a16":"ZZZZ","R":1,"D":500,"Xcm":1,"Ycm":2}})",
    R"({"TWR":{"a16":"1234","R":256,"D":500,"Xcm":1,"Ycm":2}})",
    R"({"TWR":{"a16":"1234","R":-1,"D":500,"Xcm":1,"Ycm":2}})",
    R"({"TWR":{"a16":"1234","R":1,"D":2147483648,"Xcm":1,"Ycm":2}})",
    R"({"TWR":{"a16":"1234","R":1,"D":-2147483649,"Xcm":1,"Ycm":2}})",
    R"({"TWR":{"a16":"1234","R":01,"D":500,"Xcm":1,"Ycm":2}})",
    R"({"TWR":{"a16":"1234","R":1,"D":500,"Xcm":1.5,"Ycm":2}})",
    R"({"TWR":{"a16":"1234","R":1,"D":500,"Xcm":1,"Ycm":2},})",
    R"({"TWR":{"a16":"1234","R":1,"D":500,"Xcm":1,"Ycm":2}}trailing)",
    R"({"TWR":{"a16":"1234)",
    R"({"meta":[[[[[[1]]]]]]})"
  };
  for (const auto &bad : invalid) assert(parse(frame(bad), m) == uwb::ParseResult::Invalid);
  assert(parse("JSFFFF", m) == uwb::ParseResult::Invalid);
  assert(parse("JS00Q0", m) == uwb::ParseResult::Invalid);
  assert(parse(std::string("JS0002\0}", 8), m) == uwb::ParseResult::Invalid);
  for (size_t cut = 0; cut < json.size(); ++cut) {
    // Every truncated JSON prefix must fail without reading beyond its buffer.
    assert(uwb::JsonReader(json.substr(0, cut).c_str()).parse(m) != uwb::ParseResult::Measurement);
  }
  uwb::StreamParser parser;
  parser.feed('J', 0xfffffff0, m);
  assert(!parser.expire(0x30));
  assert(parser.expire(0x60));
  unsigned reports = 0;
  const std::string two = frame(json) + frame(json);
  for (size_t i = 0; i < two.size(); ++i) {
    if (parser.feed(two[i], 100 + i / 8, m) == uwb::ParseResult::Measurement) ++reports;
  }
  assert(reports == 2);
}

static void testDji() {
  // DJI SDK v2.5 section 3.3: independent known-answer CRC/framing vector.
  const uint8_t sample[] = {
    0xaa,0x1a,0,3,0,0,0,0,0x22,0x11,0xa2,0x42,0x0e,0,
    0x20,0,0x30,0,0x40,0,1,0x14,0x7b,0x40,0x97,0xbe
  };
  const auto generated = dji::command(0x1122, 3, 0x0e, 0, sample + 14, 8);
  assert(generated.length == sizeof(sample));
  assert(memcmp(generated.bytes, sample, sizeof(sample)) == 0);
  const auto query = dji::jointQuery(0x1234);
  assert(query.length == 19 && query.bytes[14] == 2 && query.bytes[3] == 3);
  const auto speed = dji::yawSpeed(1, -150);
  assert(speed.length == 25 && speed.bytes[13] == 1 && speed.bytes[20] == 0x88);
  assert(dji::signed16(speed.bytes + 14) == -150);
  for (unsigned i = 16; i < 20; ++i) assert(speed.bytes[i] == 0);
  const auto release = dji::yawSpeed(2, 0, false);
  assert(release.bytes[20] == 0x08);

  const uint8_t replyData[] = {0,2,0x85,0xff,0,0,0x2d,0};  // -12.3, 0, 4.5 deg.
  auto reply = dji::command(0x1234, 0x20, 0x0e, 2, replyData, sizeof(replyData));
  dji::StreamParser parser;
  dji::Packet decoded;
  unsigned packets = 0;
  for (size_t i = 0; i < reply.length; ++i) {
    if (parser.feed(reply.bytes[i], 100 + i / 8, decoded)) ++packets;
  }
  assert(packets == 1);
  dji::JointAngles angles;
  assert(dji::jointReply(decoded, 0x1234, angles));
  assert(fabsf(angles.yaw + 12.3f) < .01f && angles.pitch == 4.5f);
  assert(!dji::jointReply(decoded, 0x1235, angles));
  decoded.bytes[14] = 1;
  assert(!dji::jointReply(decoded, 0x1234, angles));
  decoded.bytes[14] = 0;
  decoded.bytes[15] = 1;
  assert(!dji::jointReply(decoded, 0x1234, angles));

  // Every single-byte error in an otherwise valid response must be rejected.
  for (size_t corrupt = 0; corrupt < reply.length; ++corrupt) {
    parser.reset();
    for (size_t i = 0; i < reply.length; ++i) {
      assert(!parser.feed(reply.bytes[i] ^ (i == corrupt ? 1 : 0), 100, decoded));
    }
    // A following intact response must recover from noise/corruption.
    bool recovered = false;
    for (size_t i = 0; i < reply.length; ++i) recovered |= parser.feed(reply.bytes[i], 250, decoded);
    assert(recovered);
  }
  parser.reset();
  for (size_t i = 0; i < 8; ++i) assert(!parser.feed(reply.bytes[i], 100, decoded));
  for (size_t i = 8; i < reply.length; ++i) assert(!parser.feed(reply.bytes[i], 250, decoded));
}

static void testTracking() {
  tracking::Tracker tracker;
  assert(!tracker.update(measurement(20, 1), 100) && !tracker.fresh(100));
  tracker.select(0x2e5c);
  assert(!tracker.update(measurement(20, 1, 0xbeef), 100));
  for (unsigned i = 0; i < 3; ++i) {
    assert(tracker.update(measurement(20, 254 + i), 100 + 100 * i));
    assert(tracker.fresh(100 + 100 * i) == (i == 2));
  }
  tracker.restartOutput(300);
  assert(tracker.step(300) == 0);
  assert(fabsf(tracker.step(350) - 2.25f) < .001f);
  assert(!tracker.update(measurement(20, 0), 500)); // Duplicate doesn't keep alive.
  assert(!tracker.update(measurement(20, 255), 550)); // Old/out-of-order.
  assert(!tracker.update(measurement(20, 1, 0xbeef), 590));
  assert(!tracker.fresh(601) && tracker.step(601) == 0);
  assert(tracker.update(measurement(20, 1), 700));
  assert(!tracker.fresh(700));  // Must reacquire after silence.
  tracker.update(measurement(20, 2), 800);
  tracker.update(measurement(20, 3), 900);
  assert(tracker.fresh(900));
  assert(!tracker.update(measurement(-20, 4), 1000));
  assert(!tracker.fresh(1000) && tracker.step(1000) == 0);
  assert(!tracker.update(measurement(60, 5), 1100));
  auto tooClose = measurement(0, 6);
  tooClose.distanceCm = 50;
  assert(!tracker.update(tooClose, 1200));
  auto behind = measurement(0, 7);
  behind.yCm = -500;
  assert(!tracker.update(behind, 1300));

  tracker.select(0x2e5c);
  for (unsigned i = 0; i < 3; ++i) tracker.update(measurement(2, i), 100 + 100 * i);
  assert(tracker.step(300) == 0);  // Deadband.
  tracker.select(0x2e5c);
  tracker.update(measurement(20, 1), 0xffffff00);
  tracker.update(measurement(20, 2), 0xffffff64);
  tracker.update(measurement(20, 3), 0xffffffc8);
  assert(tracker.fresh(0x80) && !tracker.fresh(0x200));
}

static void testClosedLoop() {
  // Ideal yaw-only plant; verifies controller direction, limits and convergence,
  // not the real motor response, RF accuracy or physical mounting convention.
  tracking::Tracker tracker;
  tracker.select(0x2e5c);
  float cameraYaw = 0, previousSpeed = 0;
  uint8_t sequence = 0;
  for (uint32_t now = 0; now <= 20000; now += 10) {
    if (now % 100 == 0) tracker.update(measurement(30 - cameraYaw, sequence++), now);
    if (now == 200) tracker.restartOutput(now);
    const float speed = tracker.step(now);
    assert(fabsf(speed) <= 15.001f);
    assert(fabsf(speed - previousSpeed) <= .451f);
    cameraYaw += speed * .01f;
    previousSpeed = speed;
  }
  assert(fabsf(30 - cameraYaw) < 3.5f);
  assert(tracker.step(20301) == 0 && !tracker.fresh(20301));
  printf("Ideal 30deg step settled at %.2fdeg (3deg deadband).\n", cameraYaw);
}

static void testNoiseBounds() {
  // Exercise bounded parsers under reproducible arbitrary bytes and fragments.
  uint32_t random = 12345;
  uwb::StreamParser uwbParser;
  dji::StreamParser djiParser;
  uwb::Measurement m;
  dji::Packet packet;
  for (uint32_t i = 0; i < 100000; ++i) {
    random = random * 1664525u + 1013904223u;
    const uint8_t byte = random >> 24;
    uwbParser.feed(byte, i, m);
    djiParser.feed(byte, i, packet);
    if (i % 100 == 0) uwbParser.expire(i + 101);
  }
}

int main() {
  testUwb();
  testDji();
  testTracking();
  testClosedLoop();
  testNoiseBounds();
  puts("UWB parser, DJI framing, tracking and loss-of-signal tests passed.");
}
