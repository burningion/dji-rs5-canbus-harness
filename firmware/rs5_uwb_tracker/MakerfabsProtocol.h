#pragma once

#include <ctype.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace uwb {
struct Measurement {
  uint16_t tag = 0;
  uint8_t sequence = 0;
  int32_t distanceCm = 0;
  int32_t xCm = 0;
  int32_t yCm = 0;
};

enum class ParseResult { None, Measurement, Invalid };

// The stock Gen1 firmware emits integer-valued JSON, not raw PDoA as a bearing.
// Parse its full JSON grammar subset and require each measurement field once.
class JsonReader {
 public:
  explicit JsonReader(const char *text) : p_(text) {}
  ParseResult parse(Measurement &m) {
    if (!take('{')) return ParseResult::Invalid;
    bool found = false;
    if (!take('}')) {
      do {
        char key[24];
        if (!string(key, sizeof(key)) || !take(':')) return ParseResult::Invalid;
        if (strcmp(key, "TWR") == 0) {
          if (found || !measurement(m)) return ParseResult::Invalid;
          found = true;
        } else if (!skip(0)) {
          return ParseResult::Invalid;
        }
      } while (take(','));
      if (!take('}')) return ParseResult::Invalid;
    }
    space();
    if (*p_) return ParseResult::Invalid;
    return found ? ParseResult::Measurement : ParseResult::None;
  }

 private:
  const char *p_;
  void space() { while (*p_ && isspace(static_cast<unsigned char>(*p_))) ++p_; }
  bool take(char c) {
    space();
    if (*p_ != c) return false;
    ++p_;
    return true;
  }
  bool string(char *out, size_t capacity) {
    if (!take('"')) return false;
    size_t n = 0;
    while (*p_ && *p_ != '"') {
      // Makerfabs keys/addresses contain no escapes. Reject unexpected encoding.
      if (*p_ == '\\' || static_cast<unsigned char>(*p_) < 32) return false;
      if (out) {
        if (n + 1 >= capacity) return false;
        out[n++] = *p_;
      }
      ++p_;
    }
    if (*p_ != '"') return false;
    ++p_;
    if (out) out[n] = '\0';
    return true;
  }
  bool integer(int32_t &value) {
    space();
    bool negative = false;
    if (*p_ == '-') { negative = true; ++p_; }
    if (!isdigit(static_cast<unsigned char>(*p_))) return false;
    if (*p_ == '0' && isdigit(static_cast<unsigned char>(p_[1]))) return false;
    int64_t n = 0;
    do {
      n = n * 10 + (*p_++ - '0');
      if (n > (negative ? 2147483648LL : 2147483647LL)) return false;
    } while (isdigit(static_cast<unsigned char>(*p_)));
    value = static_cast<int32_t>(negative ? -n : n);
    return true;
  }
  bool skip(unsigned depth) {
    if (depth > 4) return false;
    space();
    if (*p_ == '"') return string(nullptr, 0);
    if (take('{')) {
      if (take('}')) return true;
      do {
        if (!string(nullptr, 0) || !take(':') || !skip(depth + 1)) return false;
      } while (take(','));
      return take('}');
    }
    if (take('[')) {
      if (take(']')) return true;
      do { if (!skip(depth + 1)) return false; } while (take(','));
      return take(']');
    }
    int32_t ignored;
    return integer(ignored);
  }
  bool measurement(Measurement &m) {
    if (!take('{')) return false;
    unsigned seen = 0;
    do {
      char key[24];
      if (!string(key, sizeof(key)) || !take(':')) return false;
      unsigned bit = 0;
      if (strcmp(key, "a16") == 0) bit = 1;
      if (strcmp(key, "R") == 0) bit = 2;
      if (strcmp(key, "D") == 0) bit = 4;
      if (strcmp(key, "Xcm") == 0) bit = 8;
      if (strcmp(key, "Ycm") == 0) bit = 16;
      if (bit && (seen & bit)) return false;
      seen |= bit;
      if (bit == 1) {
        char address[5];
        if (!string(address, sizeof(address)) || strlen(address) != 4) return false;
        unsigned tag = 0;
        for (char c : address) {
          if (!c) break;
          if (!isxdigit(static_cast<unsigned char>(c))) return false;
          tag = (tag << 4) | (isdigit(static_cast<unsigned char>(c)) ?
                c - '0' : toupper(static_cast<unsigned char>(c)) - 'A' + 10);
        }
        m.tag = static_cast<uint16_t>(tag);
      } else if (bit) {
        int32_t value;
        if (!integer(value)) return false;
        switch (bit) {
          case 2:
            if (value < 0 || value > 255) return false;
            m.sequence = static_cast<uint8_t>(value);
            break;
          case 4: m.distanceCm = value; break;
          case 8: m.xCm = value; break;
          case 16: m.yCm = value; break;
        }
      } else if (!skip(0)) return false;
    } while (take(','));
    return take('}') && seen == 31;
  }
};

// JS + four hex ASCII length digits + that many JSON bytes + optional CR/LF.
class StreamParser {
 public:
  static constexpr size_t MaxPayload = 512;
  ParseResult feed(uint8_t byte, uint32_t now, Measurement &measurement) {
    lastByteMs_ = now;
    if (state_ == 0) { if (byte == 'J') state_ = 1; return ParseResult::None; }
    if (state_ == 1) {
      state_ = byte == 'S' ? 2 : (byte == 'J' ? 1 : 0);
      length_ = digits_ = used_ = 0;
      return ParseResult::None;
    }
    if (state_ == 2) {
      if (!isxdigit(byte)) { reset(); return ParseResult::Invalid; }
      length_ = (length_ << 4) | (isdigit(byte) ? byte - '0' : toupper(byte) - 'A' + 10);
      if (++digits_ == 4) {
        if (length_ < 2 || length_ > MaxPayload) { reset(); return ParseResult::Invalid; }
        state_ = 3;
      }
      return ParseResult::None;
    }
    if (byte == 0) { reset(); return ParseResult::Invalid; }
    payload_[used_++] = static_cast<char>(byte);
    if (used_ < length_) return ParseResult::None;
    payload_[used_] = '\0';
    reset();
    return JsonReader(payload_).parse(measurement);
  }
  bool expire(uint32_t now) {
    if (state_ && now - lastByteMs_ > 100) { reset(); return true; }
    return false;
  }
  void reset() { state_ = 0; }

 private:
  unsigned state_ = 0;
  unsigned digits_ = 0;
  size_t length_ = 0, used_ = 0;
  uint32_t lastByteMs_ = 0;
  char payload_[MaxPayload + 1] = {};
};
}  // namespace uwb
