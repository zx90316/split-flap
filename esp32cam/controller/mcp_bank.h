// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapMotion.h>

namespace cam {
// A single owner performs all I2C. Transport also supplies the real commit clock.
template <class Transport> class McpBank {
 public:
  explicit McpBank(Transport& io) : io_(io) {}
  static uint8_t required(const sf::Settings& s, bool hall) {
    uint8_t mask = hall ? 8 : 0;
    for (uint8_t i = 0; i < s.count; ++i) mask |= 1 << (s.mapping[i] / 4);
    return mask;
  }
  uint32_t nowUs() const { return io_.nowUs(); }
  uint32_t nowMs() const { return io_.nowMs(); }
  void disable() { io_.reset(true); held_ = true; ready_ = false; memset(shadow_, 0, sizeof(shadow_)); }
  bool begin(uint8_t requiredMask) {
    disable(); required_ = requiredMask; present_ = 0; fault_ = nullptr;
    memset(edgeKnown_, 0, sizeof(edgeKnown_));
    io_.delayUs(20); io_.reset(false); held_ = false; io_.delayUs(1000);
    for (uint8_t chip = 0; chip < 4; ++chip)
      if (io_.probe(uint8_t(0x20 + chip))) present_ |= 1 << chip;
    if ((present_ & required_) != required_) return fail("mcp_missing");
    for (uint8_t chip = 0; chip < 4; ++chip) {
      if (!(present_ & (1 << chip))) continue;
      const uint8_t a = uint8_t(0x20 + chip);
      const uint16_t inputs = chip == 3 ? kHallPins : 0;
      uint8_t config = 255;
      if (!io_.read8(a, 0x0a, config) || config || !io_.write16(a, 0x14, 0) ||
          !io_.write16(a, 0x0c, inputs) || !io_.write16(a, 0x00, inputs)) return fail("mcp_init");
    }
    ready_ = true; auditCursor_ = 0;
    return auditAll();
  }
  bool flush(const mega::Panel& panel) {
    if (!ready_) return false;
    uint16_t words[3] = {};
    for (uint8_t u = 0; u < 10; ++u) if (panel.enabled(u))
      words[u / 4] |= uint16_t(panel.unit(u).coils()) << ((u % 4) * 4);
    const uint32_t start = nowUs();
    for (uint8_t chip = 0; chip < 3; ++chip) {
      if (!(present_ & (1 << chip)) || words[chip] == shadow_[chip]) continue;
      const uint16_t previous = shadow_[chip];
      if (!io_.write16(uint8_t(0x20 + chip), 0x14, words[chip])) return fail("i2c_write");
      shadow_[chip] = words[chip];
      const uint32_t committed = nowUs();
      for (uint8_t n = 0; n < 4 && chip * 4 + n < 10; ++n) {
        const uint8_t u = chip * 4 + n, shift = n * 4;
        if (((previous ^ words[chip]) >> shift) & 15) {
          edgeAt_[u] = committed; edgeKnown_[u] = ((words[chip] >> shift) & 15) != 0;
        }
      }
    }
    const uint32_t duration = nowUs() - start;
    if (duration > maxFlush_) maxFlush_ = duration;
    return true;
  }
  uint16_t allowSteps(const mega::Panel& panel) const {
    uint16_t mask = 0; const uint32_t now = nowUs();
    for (uint8_t u = 0; u < 10; ++u)
      if (!edgeKnown_[u] || uint32_t(now - edgeAt_[u]) >= panel.unit(u).intervalUs()) mask |= 1U << u;
    return mask;
  }
  bool halls(uint16_t& highMask) {
    uint16_t raw = 0;
    if (!ready_ || !(present_ & 8) || !io_.read16(0x23, 0x12, raw)) return fail("i2c_hall");
    highMask = uint16_t((raw & 0x7f) | ((raw & 0x700) >> 1)); return true;
  }
  bool auditOne() {
    if (!ready_) return false;
    for (uint8_t skipped = 0; skipped < 16; ++skipped) {
      const uint8_t cursor = auditCursor_; auditCursor_ = (auditCursor_ + 1) % 16;
      const uint8_t chip = cursor / 4, reg = cursor % 4;
      if (!(present_ & (1 << chip))) continue;
      const uint8_t a = uint8_t(0x20 + chip); const uint16_t inputs = chip == 3 ? kHallPins : 0;
      uint16_t value = 0; uint8_t c = 255;
      const bool ok = reg == 0 ? io_.read8(a, 0x0a, c) && c == 0 :
          io_.read16(a, reg == 1 ? 0 : reg == 2 ? 0x0c : 0x14, value) &&
          value == (reg == 3 ? (chip == 3 ? 0 : shadow_[chip]) : inputs);
      return ok || fail("mcp_state");
    }
    return true;
  }
  bool auditAll() { for (uint8_t i = 0; i < 16; ++i) if (!auditOne()) return false; return ready_; }
  bool off() const { return held_ || !(shadow_[0] | shadow_[1] | shadow_[2]); }
  bool ready() const { return ready_; }
  bool held() const { return held_; }
  uint8_t present() const { return present_; }
  uint8_t requiredMask() const { return required_; }
  uint32_t maxFlushUs() const { return maxFlush_; }
  const char* error() const { return fault_; }
 private:
  bool fail(const char* reason) { fault_ = reason; disable(); return false; }
  static const uint16_t kHallPins = 0x077f;
  Transport& io_;
  uint16_t shadow_[3] = {};
  uint32_t edgeAt_[10] = {}, maxFlush_ = 0;
  bool edgeKnown_[10] = {}, ready_ = false, held_ = true;
  uint8_t present_ = 0, required_ = 1, auditCursor_ = 0;
  const char* fault_ = nullptr;
};
}
