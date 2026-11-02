// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include "board_config.h"

// Transport implements write16/read16/write8/read8. Every operation returns ACK/length status.
// Hardware /RESET must be pulsed first; do not guess BANK after an MCU-only reset.
template <class Transport> class McpBus {
 public:
  explicit McpBus(Transport& transport) : io_(transport) {}
  bool begin() {
    healthy_ = false;
    for (uint8_t i = 0; i < 4; ++i) {
      const uint8_t address = uint8_t(0x20 + i);
      uint8_t iocon = 0xff;
      if (!io_.read8(address, 0x0a, iocon) || iocon != 0) return false;
      if (!io_.write16(address, 0x14, 0) || // clear latches before outputs
          !io_.write16(address, 0x0c, i == 3 ? board::kHallPinMask : 0) ||
          !io_.write16(address, 0x00, i == 3 ? board::kHallPinMask : 0)) return false;
    }
    for (uint8_t i = 0; i < 3; ++i) shadow_[i] = 0;
    healthy_ = audit();
    return healthy_;
  }
  bool readHalls(uint16_t& activeMask, uint16_t activeHighMask = 0) {
    if (!healthy_) return false;
    uint16_t raw = 0;
    if (!io_.read16(board::kHallAddress, 0x12, raw)) return fail();
    const uint16_t packed = uint16_t((raw & 0x007f) | ((raw & 0x0700) >> 1));
    activeMask = uint16_t((~packed ^ activeHighMask) & board::kAllUnits);
    return true;
  }
  bool flush(const uint8_t coils[10]) {
    if (!healthy_) return false;
    uint16_t desired[3] = {};
    for (uint8_t unit = 0; unit < 10; ++unit)
      desired[unit / 4] |= uint16_t(coils[unit] & 0x0f) << ((unit % 4) * 4);
    for (uint8_t chip = 0; chip < 3; ++chip) {
      if (desired[chip] == shadow_[chip]) continue;
      if (!io_.write16(board::kMotorAddresses[chip], 0x14, desired[chip])) return fail();
      shadow_[chip] = desired[chip];
    }
    return true;
  }
  bool audit() {
    // Detect a reset, stale outputs, changed configuration, and failed reads.
    for (uint8_t i = 0; i < 4; ++i) {
      const uint8_t address = uint8_t(0x20 + i);
      uint16_t direction = 0, latch = 0, pullups = 0;
      uint8_t iocon = 0xff;
      if (!io_.read8(address, 0x0a, iocon) || iocon != 0 ||
          !io_.read16(address, 0, direction) ||
          !io_.read16(address, 0x14, latch) ||
          !io_.read16(address, 0x0c, pullups) ||
          direction != (i == 3 ? board::kHallPinMask : 0) ||
          latch != (i == 3 ? 0 : shadow_[i]) ||
          pullups != (i == 3 ? board::kHallPinMask : 0)) return fail();
    }
    return true;
  }
  bool healthy() const { return healthy_; }
 private:
  bool fail() { healthy_ = false; return false; }
  Transport& io_;
  uint16_t shadow_[3] = {};
  bool healthy_ = false;
};
