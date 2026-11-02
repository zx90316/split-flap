// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "display_model.h"

namespace cam {
struct DisplayItem {
  char mode[12] = "clock", format[16] = "HH:MM", text[41] = {};
  uint16_t seconds = 30;
  Align align = Align::Left;
};
inline bool validItem(const DisplayItem& item, uint8_t width) {
  if (item.mode[11] || item.format[15] || item.text[40] || item.seconds < 10 || item.seconds > 3600 || uint8_t(item.align) > 2) return false;
  uint8_t out[10];
  return !strcmp(item.mode, "text") ? textIndices(item.text, width, item.align, false, out) : validFormat(item.mode, item.format, width);
}
inline bool inWindow(uint16_t minute, bool limited, uint16_t start, uint16_t end) {
  return !limited || start == end || (start < end ? minute >= start && minute < end : minute >= start || minute < end);
}
inline uint16_t geometrySignature(const sf::Calibration& c) {
  uint8_t data[11]; sf::encodeCalibration(c, data);
  // Speed and acceleration don't change the physical zero.
  memset(data + 4, 0, 4); return sf::crc16(data, sizeof(data));
}
inline bool contiguous(const sf::Settings& s) {
  for (uint8_t i = 0; i < s.count; ++i) if (s.mapping[i] != i) return false;
  return true;
}
}
