// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapLink.h>
#include <stdio.h>
#include <time.h>

namespace cam {
enum class Align : uint8_t { Left, Center, Right };
inline bool textIndices(const char* text, uint8_t width, Align align, bool exact, uint8_t* out) {
  if (!text || width < 1 || width > 10 || uint8_t(align) > 2) return false;
  uint8_t glyphs[10], count = 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
  while (*p) {
    if (count == width) return false;
    uint8_t index = 255, c = *p++;
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c == ' ' || c == '_') index = 0;
    else if (c >= 'A' && c <= 'Z') index = c - 'A' + 1;
    else if (c >= '0' && c <= '9') index = c - '0' + 30;
    else if (c == '$') index = 27; else if (c == '&') index = 28; else if (c == '#') index = 29;
    else if (c == ':') index = 40; else if (c == '.') index = 41; else if (c == '-') index = 42;
    else if (c == '?') index = 43; else if (c == '!') index = 44;
    else if (c == 0xc3 && *p) {
      c = *p++;
      if (c == 0x84 || c == 0xa4) index = 27;
      else if (c == 0x96 || c == 0xb6) index = 28;
      else if (c == 0x9c || c == 0xbc) index = 29;
    }
    if ((index == 1 || index == 15 || index == 21) && p[0] == 0xcc && p[1] == 0x88) {
      p += 2; index = index == 1 ? 27 : index == 15 ? 28 : 29;
    }
    if (index == 255) return false;
    glyphs[count++] = index;
  }
  if (exact && count != width) return false;
  memset(out, 0, width);
  const uint8_t start = align == Align::Right ? width - count : align == Align::Center ? (width - count) / 2 : 0;
  memcpy(out + start, glyphs, count); return true;
}
inline bool validFormat(const char* mode, const char* format, uint8_t width) {
  if (!strcmp(mode, "clock")) return (!strcmp(format, "HHMM") && width >= 4) || (!strcmp(format, "HH:MM") && width >= 5);
  if (strcmp(mode, "date")) return false;
  return (!strcmp(format, "MMDD") && width >= 4) || (!strcmp(format, "MM-DD") && width >= 5) ||
      (!strcmp(format, "YYYYMMDD") && width >= 8) || (!strcmp(format, "YYYY-MM-DD") && width >= 10);
}
inline bool formatTime(int64_t unixTime, int16_t offsetMinutes, const char* mode, const char* format,
                       uint8_t width, Align align, uint8_t* out) {
  if (unixTime < 1704067200LL || unixTime >= 4102444800LL || offsetMinutes < -720 || offsetMinutes > 840 ||
      offsetMinutes % 15 || !validFormat(mode, format, width)) return false;
  const time_t adjusted = time_t(unixTime + int64_t(offsetMinutes) * 60);
  const struct tm* utc = gmtime(&adjusted); if (!utc) return false;
  const struct tm t = *utc; char buffer[32];
  if (!strcmp(format, "HHMM")) snprintf(buffer, sizeof(buffer), "%02d%02d", t.tm_hour, t.tm_min);
  else if (!strcmp(format, "HH:MM")) snprintf(buffer, sizeof(buffer), "%02d:%02d", t.tm_hour, t.tm_min);
  else if (!strcmp(format, "MMDD")) snprintf(buffer, sizeof(buffer), "%02d%02d", t.tm_mon + 1, t.tm_mday);
  else if (!strcmp(format, "MM-DD")) snprintf(buffer, sizeof(buffer), "%02d-%02d", t.tm_mon + 1, t.tm_mday);
  else if (!strcmp(format, "YYYYMMDD")) snprintf(buffer, sizeof(buffer), "%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  else snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return textIndices(buffer, width, align, false, out);
}
static const size_t kOtaHeaderSize = 64;
inline bool otaHeader(const uint8_t* header, uint32_t maxSize, uint32_t& imageSize) {
  if (memcmp(header, "SFCAM01", 8) || sf::get32(header + 12) != sf::kVersion ||
      memcmp(header + 48, "splitflap-cam-v1", 16)) return false;
  imageSize = sf::get32(header + 8); return imageSize >= 256 && imageSize <= maxSize;
}
}
