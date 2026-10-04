#pragma once

#include "MakerfabsProtocol.h"

// Request/response bounds measurement age without synchronizing ESP32 clocks.
// Integers use explicit little-endian encoding; no native structs on the wire.
namespace wireless {
constexpr size_t RequestSize = 12, ResponseSize = 36;
constexpr uint32_t PollMs = 50, MaxRoundTripMs = 80, MaxAgeMs = 150;
enum class Result { Ignore, Invalid, Restart, Measurement };
inline void put32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(n >> (8*i));
}
inline uint32_t get32(const uint8_t *p) {
  uint32_t n = 0;
  for (unsigned i = 0; i < 4; ++i) n |= static_cast<uint32_t>(p[i]) << (8*i);
  return n;
}
inline bool header(const uint8_t *p, size_t size, size_t expected, uint8_t type) {
  return size == expected && p[0] == 'R' && p[1] == 'W' && p[2] == 1 && p[3] == type;
}
inline bool response(const uint8_t *request, size_t size, uint32_t boot,
                     const uwb::Measurement &m, uint32_t age, bool valid,
                     uint8_t *out) {
  if (!header(request, size, RequestSize, 1)) return false;
  memcpy(out, request, RequestSize); out[3] = 2;
  put32(out+12, boot); put32(out+16, age);
  out[20] = static_cast<uint8_t>(m.tag); out[21] = static_cast<uint8_t>(m.tag >> 8);
  out[22] = m.sequence; out[23] = valid ? 1 : 0;
  put32(out+24, static_cast<uint32_t>(m.distanceCm));
  put32(out+28, static_cast<uint32_t>(m.xCm));
  put32(out+32, static_cast<uint32_t>(m.yCm));
  return true;
}
class Receiver {
 public:
  void begin(uint32_t session) { *this = Receiver(); session_ = session; }
  void cancel() { pending_ = false; }
  void request(uint32_t now, uint8_t *out) {
    out[0]='R'; out[1]='W'; out[2]=1; out[3]=1;
    put32(out+4, session_); put32(out+8, ++sequence_);
    sentMs_=now; pending_=true;
  }
  Result accept(const uint8_t *p, size_t size, uint32_t now,
                uwb::Measurement &m, uint32_t &sampleMs) {
    if (!header(p,size,ResponseSize,2) || !pending_ ||
        get32(p+4)!=session_ || get32(p+8)!=sequence_) return Result::Ignore;
    pending_=false; // One response per challenge, including invalid responses.
    const uint32_t rtt=now-sentMs_, age=get32(p+16), boot=get32(p+12);
    if (rtt>MaxRoundTripMs || age>MaxAgeMs || rtt+age>MaxAgeMs || p[23]!=1)
      return Result::Invalid;
    if (!haveBoot_ || boot!=boot_) {
      boot_=boot; haveBoot_=true; haveSample_=false;
      return Result::Restart; // Invalidate and reacquire after either board boots.
    }
    m.tag=static_cast<uint16_t>(p[20] | (p[21]<<8)); m.sequence=p[22];
    m.distanceCm=static_cast<int32_t>(get32(p+24));
    m.xCm=static_cast<int32_t>(get32(p+28)); m.yCm=static_cast<int32_t>(get32(p+32));
    sampleMs=now-(rtt+age); // Conservative age includes the whole round trip.
    // Changes in RTT must not move the tracker clock backwards.
    if (haveSample_ && static_cast<int32_t>(sampleMs-lastSampleMs_)<0) return Result::Ignore;
    lastSampleMs_=sampleMs; haveSample_=true;
    return Result::Measurement;
  }
 private:
  uint32_t session_=0, sequence_=0, sentMs_=0, boot_=0, lastSampleMs_=0;
  bool pending_=false, haveBoot_=false, haveSample_=false;
};
} // namespace wireless
