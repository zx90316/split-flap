// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace sf {
static const uint8_t kVersion = 1, kMaxUnits = 10, kAll = 255;
static const size_t kMaxPayload = 160, kRawSize = kMaxPayload + 13, kWireSize = kRawSize + 2;
static const size_t kSettingsSize = 121;
enum class Kind : uint8_t { Boot = 1, Hello, Welcome, Ping, Pong, Command, Result, Query, Panel, Unit, Config };
enum class Stage : uint8_t { Accepted = 1, Done, Rejected, Fault };
enum class Error : uint8_t { None, Busy, Invalid, Readiness, Session, Protocol, LinkLost, UsbOverride, Storage, Stopped, Unknown };
enum class Op : uint8_t { Stop = 1, Arm, SetIndex, SetText, Text, Step, Rev, Flap, Goto, Home,
  Measure, Cycle, Led, Watch, Select, Digits, Units, Speed, Accel, Revolution, Offset, Direction,
  Order, Polarity, Save, Load, ApplyConfig, Claim };
inline const char* errorName(Error e) {
  switch (e) {
    case Error::None: return "none"; case Error::Busy: return "busy";
    case Error::Invalid: return "invalid"; case Error::Readiness: return "not_ready";
    case Error::Session: return "session"; case Error::Protocol: return "protocol";
    case Error::LinkLost: return "link_lost"; case Error::UsbOverride: return "usb_override";
    case Error::Storage: return "storage"; case Error::Stopped: return "stopped";
    default: return "unknown";
  }
}
inline void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) { put16(p, uint16_t(v)); put16(p + 2, uint16_t(v >> 16)); }
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8); }
inline uint32_t get32(const uint8_t* p) { return uint32_t(get16(p)) | (uint32_t(get16(p + 2)) << 16); }
inline uint16_t crc16(const uint8_t* p, size_t n) {
  uint16_t crc = 0xffff;
  while (n--) { crc ^= uint16_t(*p++) << 8;
    for (uint8_t i = 0; i < 8; ++i) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}
struct Packet {
  uint8_t version = kVersion;
  Kind kind = Kind::Boot;
  uint32_t session = 0, id = 0;
  uint8_t length = 0;
  uint8_t data[kMaxPayload] = {};
};
inline size_t encode(const Packet& p, uint8_t* wire) {
  if (p.length > kMaxPayload) return 0;
  uint8_t raw[kRawSize];
  raw[0] = p.version; raw[1] = uint8_t(p.kind); put32(raw + 2, p.session); put32(raw + 6, p.id);
  raw[10] = p.length; memcpy(raw + 11, p.data, p.length);
  const size_t size = size_t(p.length) + 13;
  put16(raw + size - 2, crc16(raw, size - 2));
  size_t codeAt = 0, out = 1; uint8_t code = 1;
  for (size_t i = 0; i < size; ++i) {
    if (raw[i] == 0) { wire[codeAt] = code; codeAt = out++; code = 1; }
    else { wire[out++] = raw[i]; ++code; }
  }
  wire[codeAt] = code; wire[out++] = 0; return out;
}
class Decoder {
 public:
  bool feed(uint8_t ch, uint32_t now, Packet& p) {
    if (length_ && uint32_t(now - last_) > 100) { length_ = 0; discard_ = true; }
    last_ = now;
    if (ch) {
      if (!discard_) {
        if (length_ == sizeof(wire_)) { discard_ = true; length_ = 0; }
        else wire_[length_++] = ch;
      }
      return false;
    }
    if (discard_) { discard_ = false; length_ = 0; return false; }
    uint8_t raw[kRawSize]; size_t out = 0, in = 0;
    const size_t size = length_; length_ = 0;
    while (in < size) {
      const uint8_t code = wire_[in++];
      if (!code || size_t(code - 1) > size - in) return false;
      for (uint8_t j = 1; j < code; ++j) { if (out == sizeof(raw)) return false; raw[out++] = wire_[in++]; }
      if (code != 255 && in < size) { if (out == sizeof(raw)) return false; raw[out++] = 0; }
    }
    if (out < 13 || raw[10] > kMaxPayload || out != size_t(raw[10]) + 13 ||
        get16(raw + out - 2) != crc16(raw, out - 2)) return false;
    p.version = raw[0]; p.kind = Kind(raw[1]); p.session = get32(raw + 2); p.id = get32(raw + 6);
    p.length = raw[10]; memcpy(p.data, raw + 11, p.length); return true;
  }
 private:
  uint8_t wire_[kWireSize]; uint16_t length_ = 0; uint32_t last_ = 0; bool discard_ = false;
};
struct Calibration {
  uint16_t revolution = 4096, offset = 0, speed = 800, accel = 800;
  int8_t direction = 1;
  uint8_t order[4] = {0, 1, 2, 3};
  bool activeLow = true;
};
struct Settings {
  uint8_t count = 1, mapping[10] = {};
  Calibration units[10];
};
inline bool validCalibration(const Calibration& c) {
  if (c.revolution < 1000 || c.revolution > 20000 || c.offset >= c.revolution ||
      c.speed < 800 || c.speed > 30000 || c.accel < 100 || c.accel > 6000 ||
      (c.direction != 1 && c.direction != -1)) return false;
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 4; ++i) { if (c.order[i] > 3 || (mask & (1 << c.order[i]))) return false; mask |= 1 << c.order[i]; }
  return mask == 15;
}
inline bool validSettings(const Settings& s) {
  if (!s.count || s.count > 10) return false;
  uint16_t mask = 0;
  for (uint8_t i = 0; i < s.count; ++i) {
    if (s.mapping[i] > 9 || (mask & (1U << s.mapping[i]))) return false;
    mask |= 1U << s.mapping[i];
  }
  for (uint8_t u = 0; u < 10; ++u) if (!validCalibration(s.units[u])) return false;
  return true;
}
inline void encodeCalibration(const Calibration& c, uint8_t* out) {
  put16(out, c.revolution); put16(out + 2, c.offset); put16(out + 4, c.speed); put16(out + 6, c.accel);
  out[8] = uint8_t(c.direction); out[9] = 0;
  for (uint8_t i = 0; i < 4; ++i) out[9] |= c.order[i] << (2 * i);
  out[10] = c.activeLow;
}
inline bool decodeCalibration(const uint8_t* in, Calibration& c) {
  c.revolution = get16(in); c.offset = get16(in + 2); c.speed = get16(in + 4); c.accel = get16(in + 6);
  c.direction = int8_t(in[8]);
  for (uint8_t i = 0; i < 4; ++i) c.order[i] = (in[9] >> (2 * i)) & 3;
  c.activeLow = in[10] != 0;
  return in[10] <= 1 && validCalibration(c);
}
inline void encodeSettings(const Settings& s, uint8_t* out) {
  out[0] = s.count;
  for (uint8_t i = 0; i < 10; ++i) out[1 + i] = i < s.count ? s.mapping[i] : 0;
  for (uint8_t i = 0; i < 10; ++i) encodeCalibration(s.units[i], out + 11 + 11 * i);
}
inline bool decodeSettings(const uint8_t* in, size_t length, Settings& s) {
  if (length != kSettingsSize) return false;
  s.count = in[0]; memcpy(s.mapping, in + 1, 10);
  for (uint8_t i = 0; i < 10; ++i) if (!decodeCalibration(in + 11 + 11 * i, s.units[i])) return false;
  return validSettings(s);
}
struct PanelState {
  uint8_t count = 1, mapping[10] = {}, selected = 0;
  bool busy = false, saving = false, remote = false, usbLocked = false, watching = false;
  uint32_t usbRevision = 0, configRevision = 0;
  uint8_t faultUnit = 10;
  char error[24] = {};
};
static const uint8_t kPanelSize = 50;
inline void encodePanel(const PanelState& s, uint8_t* out) {
  memset(out, 0, kPanelSize); out[0] = s.count; memcpy(out + 1, s.mapping, 10); out[11] = s.selected;
  out[12] = uint8_t(s.busy) | (uint8_t(s.saving) << 1) | (uint8_t(s.remote) << 2) |
    (uint8_t(s.usbLocked) << 3) | (uint8_t(s.watching) << 4);
  put32(out + 13, s.usbRevision); put32(out + 17, s.configRevision); out[21] = s.faultUnit;
  memcpy(out + 22, s.error, 24);
}
inline bool decodePanel(const uint8_t* in, size_t n, PanelState& s) {
  if (n != kPanelSize || !in[0] || in[0] > 10 || in[11] > 9) return false;
  s.count = in[0]; memcpy(s.mapping, in + 1, 10); s.selected = in[11];
  uint16_t mask = 0;
  for (uint8_t i = 0; i < s.count; ++i) {
    if (s.mapping[i] > 9 || (mask & (1U << s.mapping[i]))) return false;
    mask |= 1U << s.mapping[i];
  }
  s.busy = in[12] & 1; s.saving = in[12] & 2; s.remote = in[12] & 4;
  s.usbLocked = in[12] & 8; s.watching = in[12] & 16;
  s.usbRevision = get32(in + 13); s.configRevision = get32(in + 17); s.faultUnit = in[21];
  memcpy(s.error, in + 22, 24); s.error[23] = 0; return true;
}
struct UnitState {
  uint8_t unit = 0, mode = 0, reference = 0, index = 0, target = 0;
  bool enabled = false, armed = false, rawHigh = true, hall = false;
  uint32_t steps = 0;
  uint16_t remaining = 0, measured[3] = {};
  uint8_t measuredCount = 0;
  Calibration config;
  char error[24] = {};
};
static const uint8_t kUnitSize = 57;
inline void encodeUnit(const UnitState& s, uint8_t* out) {
  memset(out, 0, kUnitSize); out[0] = s.unit; out[1] = s.mode; out[2] = s.reference;
  out[3] = s.index; out[4] = s.target;
  out[5] = uint8_t(s.enabled) | (uint8_t(s.armed) << 1) | (uint8_t(s.rawHigh) << 2) | (uint8_t(s.hall) << 3);
  put32(out + 6, s.steps); put16(out + 10, s.remaining); out[12] = s.measuredCount;
  for (uint8_t i = 0; i < 3; ++i) put16(out + 13 + 2 * i, s.measured[i]);
  encodeCalibration(s.config, out + 19); memcpy(out + 30, s.error, 24);
}
inline bool decodeUnit(const uint8_t* in, size_t n, UnitState& s) {
  if (n != kUnitSize || in[0] > 9 || in[1] > 10 || in[2] > 2 || in[3] > 44 || in[4] > 44 || in[12] > 3) return false;
  s.unit = in[0]; s.mode = in[1]; s.reference = in[2]; s.index = in[3]; s.target = in[4];
  s.enabled = in[5] & 1; s.armed = in[5] & 2; s.rawHigh = in[5] & 4; s.hall = in[5] & 8;
  s.steps = get32(in + 6); s.remaining = get16(in + 10); s.measuredCount = in[12];
  for (uint8_t i = 0; i < 3; ++i) s.measured[i] = get16(in + 13 + 2 * i);
  memcpy(s.error, in + 30, 24); s.error[23] = 0;
  return decodeCalibration(in + 19, s.config);
}
}  // namespace sf
