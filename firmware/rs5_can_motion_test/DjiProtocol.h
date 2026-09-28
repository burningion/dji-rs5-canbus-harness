#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace dji {
static constexpr size_t MaxPacket = 128;
static constexpr uint32_t TxCanId = 0x223;
static constexpr uint32_t RxCanId = 0x222;

inline uint16_t read16(const uint8_t *p) { return p[0] | (static_cast<uint16_t>(p[1]) << 8); }
inline uint32_t read32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline int16_t signed16(const uint8_t *p) {
  const uint16_t n = read16(p);
  return static_cast<int16_t>(n < 0x8000 ? static_cast<int32_t>(n) : static_cast<int32_t>(n) - 65536);
}
inline void write16(uint8_t *p, uint16_t n) { p[0] = n & 255; p[1] = n >> 8; }
inline void write32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = (n >> (8 * i)) & 255;
}
inline uint16_t crc16(const uint8_t *data, size_t length) {
  // Reflected implementation: reflect the SDK's XorIn 0xc55c first.
  // Matches crc_init() in DJI's supplied custom_crc16.h.
  uint16_t crc = 0x3aa3;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
  }
  return crc;
}
inline uint32_t crc32(const uint8_t *data, size_t length) {
  // Reflected XorIn 0xc55c0000, per DJI's custom_crc32.h.
  uint32_t crc = 0x00003aa3;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return crc;
}

struct Packet {
  uint8_t bytes[MaxPacket] = {};
  size_t length = 0;
};

inline Packet command(uint16_t sequence, uint8_t type, uint8_t set, uint8_t id,
                      const uint8_t *payload, size_t payloadSize) {
  Packet packet;
  if (payloadSize > MaxPacket - 18) return packet;
  packet.length = 18 + payloadSize;
  uint8_t *p = packet.bytes;
  p[0] = 0xaa;
  write16(p + 1, static_cast<uint16_t>(packet.length));
  p[3] = type;
  write16(p + 8, sequence);
  write16(p + 10, crc16(p, 10));
  p[12] = set;
  p[13] = id;
  if (payloadSize) memcpy(p + 14, payload, payloadSize);
  write32(p + packet.length - 4, crc32(p, packet.length - 4));
  return packet;
}

inline Packet jointQuery(uint16_t sequence) {
  const uint8_t payload[] = {0x02};
  return command(sequence, 0x03, 0x0e, 0x02, payload, sizeof(payload));
}
inline Packet yawSpeed(uint16_t sequence, int16_t tenthsPerSecond, bool takeControl = true) {
  uint8_t payload[7] = {};
  write16(payload, static_cast<uint16_t>(tenthsPerSecond));
  // SDK: bit7 takes/relinquishes speed control; bit3 ignores lens focal length.
  payload[6] = takeControl ? 0x88 : 0x08;
  return command(sequence, 0x00, 0x0e, 0x01, payload, sizeof(payload));
}

struct JointAngles {
  float yaw = 0, roll = 0, pitch = 0;
};
inline bool jointReply(const Packet &packet, uint16_t expectedSequence, JointAngles &angles) {
  const uint8_t *p = packet.bytes;
  if (packet.length != 26 || !(p[3] & 0x20) || read16(p + 8) != expectedSequence ||
      p[12] != 0x0e || p[13] != 0x02 || p[14] != 0 || p[15] != 0x02) return false;
  const int16_t yaw = signed16(p + 16), roll = signed16(p + 18), pitch = signed16(p + 20);
  if (yaw < -1800 || yaw > 1800 || roll < -1800 || roll > 1800 || pitch < -1800 || pitch > 1800) return false;
  angles.yaw = yaw / 10.0f;
  angles.roll = roll / 10.0f;
  angles.pitch = pitch / 10.0f;
  return true;
}

// Reassemble a byte stream split across standard CAN frames. Header/body CRCs
// bound false synchronization; a gap or an observed dropped CAN frame resets it.
class StreamParser {
 public:
  bool feed(uint8_t byte, uint32_t now, Packet &out) {
    if (used_ && now - lastByteMs_ > 100) reset();
    lastByteMs_ = now;
    if (used_ == MaxPacket) discard(1);
    buffer_[used_++] = byte;
    while (used_) {
      if (buffer_[0] != 0xaa) { discard(1); continue; }
      if (used_ < 3) return false;
      const uint16_t descriptor = read16(buffer_ + 1);
      const size_t length = descriptor & 0x3ff;
      if ((descriptor >> 10) != 0 || length < 18 || length > MaxPacket) {
        discard(1);
        continue;
      }
      if (used_ < 12) return false;
      if (buffer_[4] != 0 || crc16(buffer_, 10) != read16(buffer_ + 10)) {
        discard(1);
        continue;
      }
      if (used_ < length) return false;
      if (crc32(buffer_, length - 4) != read32(buffer_ + length - 4)) {
        discard(1);
        continue;
      }
      memcpy(out.bytes, buffer_, length);
      out.length = length;
      discard(length);
      return true;
    }
    return false;
  }
  void reset() { used_ = 0; }
 private:
  uint8_t buffer_[MaxPacket] = {};
  size_t used_ = 0;
  uint32_t lastByteMs_ = 0;
  void discard(size_t length) {
    used_ -= length;
    memmove(buffer_, buffer_ + length, used_);
  }
};
}  // namespace dji
