// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapLink.h>

namespace cam {
// Last completed glyphs are history, never evidence of a rotor's position after a reset.
// Physical unit IDs keep their history when the display order or width changes.
class DisplayMemory {
 public:
  static const uint8_t kUnknown = 255, kSize = 13;
  DisplayMemory() { memset(glyphs_, kUnknown, sizeof(glyphs_)); }
  uint8_t glyph(uint8_t unit) const { return unit < 10 ? glyphs_[unit] : kUnknown; }
  bool dirty() const { return dirty_; }
  void saved() { dirty_ = false; }
  bool observe(const sf::UnitState* units) {
    if (!units) return false;
    for (uint8_t u = 0; u < 10; ++u) {
      const sf::UnitState& unit = units[u];
      if (unit.reference && !unit.mode && unit.index < 45 && glyphs_[u] != unit.index) {
        glyphs_[u] = unit.index; dirty_ = true;
      }
    }
    return dirty_;
  }
  void encode(uint8_t* data) const {
    data[0] = 1; memcpy(data + 1, glyphs_, 10);
    sf::put16(data + 11, sf::crc16(data, 11));
  }
  bool decode(const uint8_t* data, size_t size) {
    if (!data || size != kSize || data[0] != 1 || sf::get16(data + 11) != sf::crc16(data, 11)) return false;
    for (uint8_t u = 0; u < 10; ++u) if (data[u + 1] != kUnknown && data[u + 1] >= 45) return false;
    memcpy(glyphs_, data + 1, 10); dirty_ = false; return true;
  }
 private:
  uint8_t glyphs_[10];
  bool dirty_ = false;
};
}
