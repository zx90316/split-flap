// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapLink.h>
#include "board_profile.h"
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
  if (!mode || !format || !width || width > 10) return false;
  if (!strcmp(mode, "weekday")) return (!strcmp(format, "SHORT") && width >= 3) || (!strcmp(format, "LONG") && width >= 9);
  const char* clocks[] = {"HH", "MM", "SS", "HHMM", "HH:MM", "HHMMSS", "HH:MM:SS", "hhmm", "hh:mm", "hhmmss", "hh:mm:ss", "hhmmAP", "hh:mmAP", "hhmmssAP", "hh:mm:ssAP"};
  const char* dates[] = {"MMDD", "MM-DD", "DDMM", "DD-MM", "YYMMDD", "YYYYMMDD", "YYYY-MM-DD", "DD-MM-YYYY"};
  if (!strcmp(mode, "clock")) {
    for (const char* f : clocks) if (!strcmp(format, f)) return strlen(f) <= width;
  } else if (!strcmp(mode, "date")) {
    for (const char* f : dates) if (!strcmp(format, f)) return strlen(f) <= width;
  }
  return false;
}
inline bool formatTime(int64_t unixTime, int16_t offsetMinutes, const char* mode, const char* format,
                       uint8_t width, Align align, uint8_t* out, bool leadingZero = true) {
  if (unixTime < 1704067200LL || unixTime >= 4102444800LL || offsetMinutes < -720 || offsetMinutes > 840 ||
      offsetMinutes % 15 || !validFormat(mode, format, width)) return false;
  const time_t adjusted = time_t(unixTime + int64_t(offsetMinutes) * 60);
  const struct tm* utc = gmtime(&adjusted); if (!utc) return false;
  const struct tm t = *utc; char buffer[32] = {}; size_t at = 0;
  if (!strcmp(mode, "weekday")) {
    const char* names[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
    snprintf(buffer, sizeof(buffer), "%s", names[t.tm_wday]);
    if (!strcmp(format, "SHORT")) buffer[3] = 0;
  } else for (const char* p = format; *p && at < sizeof(buffer) - 5;) {
    int value = -1, digits = 2;
    if (!strncmp(p, "YYYY", 4)) { value = t.tm_year + 1900; digits = 4; p += 4; }
    else if (!strncmp(p, "YY", 2)) { value = (t.tm_year + 1900) % 100; p += 2; }
    else if (!strncmp(p, "HH", 2)) { value = t.tm_hour; p += 2; }
    else if (!strncmp(p, "hh", 2)) { value = t.tm_hour % 12; if (!value) value = 12; p += 2; }
    else if (!strncmp(p, "MM", 2) || !strncmp(p, "mm", 2)) { value = !strcmp(mode, "date") ? t.tm_mon + 1 : t.tm_min; p += 2; }
    else if (!strncmp(p, "SS", 2) || !strncmp(p, "ss", 2)) { value = t.tm_sec; p += 2; }
    else if (!strncmp(p, "DD", 2)) { value = t.tm_mday; p += 2; }
    else if (!strncmp(p, "AP", 2)) { buffer[at++] = t.tm_hour < 12 ? 'A' : 'P'; buffer[at++] = 'M'; p += 2; }
    else buffer[at++] = *p++;
    if (value >= 0) at += snprintf(buffer + at, sizeof(buffer) - at, "%0*d", digits, value);
    buffer[at] = 0;
  }
  if (!leadingZero && (!strncmp(format, "HH", 2) || !strncmp(format, "hh", 2)) && buffer[0] == '0') buffer[0] = ' ';
  return textIndices(buffer, width, align, false, out);
}
static const size_t kOtaHeaderSize = 64;
inline bool otaHeader(const uint8_t* header, uint32_t maxSize, uint32_t& imageSize) {
  if (memcmp(header, kOtaMagic, 8) || sf::get32(header + 12) != sf::kVersion ||
      memcmp(header + 48, kOtaProject, 16)) return false;
  imageSize = sf::get32(header + 8); return imageSize >= 256 && imageSize <= maxSize;
}
}
