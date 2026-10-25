// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "bench.h"
#include <SplitFlapLink.h>

namespace mega {
static const uint8_t kMaxUnits = 10;
enum class Batch : uint8_t { None, Text, Home, Cycle };

// Independent unit state, explicit installation map, concurrent batch start.
class Panel {
 public:
  Panel();
  bool setDigits(uint8_t count);
  bool setUnits(const uint8_t* units, uint8_t count);
  bool enabled(uint8_t unit) const;
  uint8_t digits() const { return count_; }
  uint8_t physicalUnit(uint8_t digit) const { return digit < count_ ? mapping_[digit] : kMaxUnits; }
  uint8_t selectedUnit() const { return selected_; }
  bool select(uint8_t unit);
  Bench& selected() { return units_[selected_]; }
  const Bench& selected() const { return units_[selected_]; }
  Bench& unit(uint8_t unit) { return units_[unit]; }  // Caller bounds-checks physical IDs.
  const Bench& unit(uint8_t unit) const { return units_[unit]; }
  bool activeLow(uint8_t unit) const { return polarity_[unit]; }
  bool setPolarity(bool activeLow);
  bool speedAll(uint16_t interval);
  bool accelerationAll(uint16_t acceleration);
  bool armAll();
  void stopAll();
  bool setCurrentText(const uint8_t* values, uint8_t count);
  void settings(sf::Settings& out) const;
  bool applySettings(const sf::Settings& settings);
  bool text(const uint8_t* values, uint8_t count, uint32_t now);
  bool homeAll(uint32_t now);
  bool cycleAll(uint32_t now);
  void tick(uint32_t now, uint16_t rawHighMask);
  bool busy() const;
  bool batching() const { return batch_ != Batch::None; }
  Batch batch() const { return batch_; }
  const char* error() const { return error_; }
  uint8_t faultUnit() const { return faultUnit_; }
  void clearError() { error_ = nullptr; faultUnit_ = kMaxUnits; }

 private:
  bool ready(bool needsPosition) const;
  void startBatch(uint32_t now);
  void fail(uint8_t unit, const char* reason);
  Bench units_[kMaxUnits];
  bool polarity_[kMaxUnits];
  uint8_t mapping_[kMaxUnits] = {0};
  uint8_t targets_[kMaxUnits] = {0};
  uint8_t count_ = 1, selected_ = 0, cursor_ = 0, faultUnit_ = kMaxUnits;
  uint16_t enabledMask_ = 1;
  Batch batch_ = Batch::None;
  const char* error_ = nullptr;
};

// Bulk values use one ASCII glyph per position; underscore and space mean blank.
inline bool parseText(const char* text, uint8_t width, bool exact, uint8_t* values) {
  if (!text || !values || width == 0 || width > kMaxUnits) return false;
  const size_t length = strlen(text);
  if (length > width || (exact && length != width)) return false;
  uint8_t parsed[kMaxUnits] = {};
  for (uint8_t i = 0; i < length; ++i) {
    if (text[i] == '_' || text[i] == ' ') continue;
    const char glyph[] = {text[i], 0};
    if (!indexFor(glyph, parsed[i])) return false;
  }
  for (uint8_t i = 0; i < width; ++i) values[i] = parsed[i];
  return true;
}
}
