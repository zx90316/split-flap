// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include <string.h>

namespace mega {
static const uint8_t kFlapCount = 45;

// Original 45-flap order. $ / & / # occupy the AE / OE / UE slots.
inline char characterFor(uint8_t index) {
  static const char characters[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$&#0123456789:.-?!";
  static_assert(sizeof(characters) == kFlapCount + 1, "Flap map must have 45 entries");
  return index < kFlapCount ? characters[index] : '\0';
}

// Serial input is ASCII and uppercased by the adapter. Digits mean glyphs,
// whereas GOTO/SETINDEX arguments mean numeric slot indices.
inline bool indexFor(const char* value, uint8_t& index) {
  if (!value || !*value) return false;
  if (!strcmp(value, "BLANK") || !strcmp(value, "SPACE")) { index = 0; return true; }
  if (!strcmp(value, "AE")) { index = 27; return true; }
  if (!strcmp(value, "OE")) { index = 28; return true; }
  if (!strcmp(value, "UE")) { index = 29; return true; }
  if (value[1]) return false;
  for (uint8_t i = 1; i < kFlapCount; ++i) {
    if (characterFor(i) == value[0]) { index = i; return true; }
  }
  return false;
}
}
