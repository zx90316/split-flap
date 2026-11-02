// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "display_model.h"
#include "display_memory.h"
#include "display_program.h"

namespace cam {
struct LegacySettings {
  uint32_t version = 1;
  char ssid[33] = {}, wifiPassword[65] = {}, apPassword[64] = {}, adminPassword[64] = {};
  char mode[8] = "text", clockFormat[8] = "HH:MM", dateFormat[16] = "YYYY-MM-DD";
  int16_t timezone = 480;
  Align align = Align::Left;
};
struct AppSettings {
  uint32_t version = 2;
  char ssid[33] = {}, wifiPassword[65] = {}, apPassword[64] = {}, adminPassword[64] = {};
  char mode[12] = "text", clockFormat[16] = "HH:MM", dateFormat[16] = "YYYY-MM-DD", weekdayFormat[8] = "SHORT";
  int16_t timezone = 480;
  Align align = Align::Left;
  bool leadingZero = true, limited = false, autoRecovery = false, recoveryChosen = false, resumeDisplay = false;
  uint16_t windowStart = 0, windowEnd = 0, calibratedMask = 0, signatures[10] = {};
  uint8_t itemCount = 1, itemIndex = 0;
  DisplayItem items[8];
};
inline void randomPassword(char* out, size_t length) {
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  for (size_t i = 0; i < length; ++i) out[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
  out[length] = 0;
}
inline bool validLegacySettings(const LegacySettings& s) {
  return s.version == 1 && s.ssid[32] == 0 && s.wifiPassword[64] == 0 && s.apPassword[63] == 0 && s.adminPassword[63] == 0 &&
      strlen(s.apPassword) >= 12 && strlen(s.adminPassword) >= 12 && s.mode[7] == 0 && s.clockFormat[7] == 0 && s.dateFormat[15] == 0 &&
      (!strcmp(s.mode, "text") || !strcmp(s.mode, "clock") || !strcmp(s.mode, "date")) &&
      validFormat("clock", s.clockFormat, 10) && validFormat("date", s.dateFormat, 10) &&
      uint8_t(s.align) <= 2 && s.timezone >= -720 && s.timezone <= 840 && s.timezone % 15 == 0;
}
inline bool validAppSettings(const AppSettings& s) {
  if (s.version != 2 || s.ssid[32] || s.wifiPassword[64] || s.apPassword[63] || s.adminPassword[63] ||
      strlen(s.apPassword) < 12 || strlen(s.adminPassword) < 12 || s.mode[11] || s.clockFormat[15] || s.dateFormat[15] || s.weekdayFormat[7] ||
      (strcmp(s.mode, "text") && strcmp(s.mode, "clock") && strcmp(s.mode, "date") && strcmp(s.mode, "weekday") && strcmp(s.mode, "carousel")) ||
      !validFormat("clock", s.clockFormat, 10) || !validFormat("date", s.dateFormat, 10) || !validFormat("weekday", s.weekdayFormat, 10) ||
      uint8_t(s.align) > 2 || s.timezone < -720 || s.timezone > 840 || s.timezone % 15 || s.windowStart > 1439 || s.windowEnd > 1439 ||
      s.itemCount < 1 || s.itemCount > 8 || s.itemIndex >= s.itemCount || (s.calibratedMask & ~0x3ff)) return false;
  for (uint8_t i = 0; i < s.itemCount; ++i) if (!validItem(s.items[i], 10)) return false;
  return true;
}
inline bool calibrated(const AppSettings& app, const sf::Settings& mega, uint8_t unit = sf::kAll) {
  if (!contiguous(mega)) return false;
  for (uint8_t i = 0; i < mega.count; ++i) if (unit == sf::kAll || unit == i)
    if (!(app.calibratedMask & (1U << i)) || app.signatures[i] != geometrySignature(mega.units[i])) return false;
  return unit == sf::kAll || unit < mega.count;
}
inline bool programWindowOpen(const AppSettings& s, int64_t utc) {
  if (!s.limited) return true;
  if (utc < 1704067200LL || utc >= 4102444800LL) return false;
  const uint16_t minute = uint16_t(((utc + int64_t(s.timezone) * 60) % 86400 + 86400) % 86400 / 60);
  return inWindow(minute, true, s.windowStart, s.windowEnd);
}
class AppStore {
 public:
  bool begin(AppSettings& s) {
    if (!prefs_.begin("flap-cam", false)) return false;
    uint8_t data[sizeof(AppSettings) + 2];
    if (prefs_.getBytesLength("settings-v2")) {
      if (prefs_.getBytesLength("settings-v2") != sizeof(data) || prefs_.getBytes("settings-v2", data, sizeof(data)) != sizeof(data) ||
          sf::get16(data + sizeof(s)) != sf::crc16(data, sizeof(s))) return false;
      AppSettings saved; memcpy(&saved, data, sizeof(saved));
      if (validAppSettings(saved)) { s = saved; return true; }
      return false;
    }
    uint8_t old[sizeof(LegacySettings) + 2];
    if (prefs_.getBytesLength("settings")) {
      if (prefs_.getBytesLength("settings") != sizeof(old) || prefs_.getBytes("settings", old, sizeof(old)) != sizeof(old) ||
          sf::get16(old + sizeof(LegacySettings)) != sf::crc16(old, sizeof(LegacySettings))) return false;
      LegacySettings prior; memcpy(&prior, old, sizeof(prior)); if (!validLegacySettings(prior)) return false;
      memcpy(s.ssid, prior.ssid, sizeof(s.ssid)); memcpy(s.wifiPassword, prior.wifiPassword, sizeof(s.wifiPassword));
      memcpy(s.apPassword, prior.apPassword, sizeof(s.apPassword)); memcpy(s.adminPassword, prior.adminPassword, sizeof(s.adminPassword));
      strcpy(s.mode, prior.mode); strcpy(s.clockFormat, prior.clockFormat); strcpy(s.dateFormat, prior.dateFormat);
      s.timezone = prior.timezone; s.align = prior.align;
      return save(s); // Leave the v1 record intact for recovery; never regenerate migrated passwords.
    }
    randomPassword(s.apPassword, 12); randomPassword(s.adminPassword, 12); fresh_ = true;
    return save(s);
  }
  bool save(const AppSettings& s) {
    if (!validAppSettings(s)) return false;
    uint8_t data[sizeof(s) + 2]; memcpy(data, &s, sizeof(s)); sf::put16(data + sizeof(s), sf::crc16(data, sizeof(s)));
    return prefs_.putBytes("settings-v2", data, sizeof(data)) == sizeof(data);
  }
  bool fresh() const { return fresh_; }
  void loadDisplayMemory(DisplayMemory& memory) {
    uint8_t data[DisplayMemory::kSize];
    if (prefs_.getBytesLength("last-glyphs") == sizeof(data) && prefs_.getBytes("last-glyphs", data, sizeof(data)) == sizeof(data))
      memory.decode(data, sizeof(data));
  }
  bool saveDisplayMemory(const DisplayMemory& memory) {
    uint8_t data[DisplayMemory::kSize]; memory.encode(data);
    return prefs_.putBytes("last-glyphs", data, sizeof(data)) == sizeof(data);
  }
  bool readSessions(void* data, size_t size) { return prefs_.getBytesLength("sessions") == size && prefs_.getBytes("sessions", data, size) == size; }
  bool writeSessions(const void* data, size_t size) { return prefs_.putBytes("sessions", data, size) == size; }
 private:
  Preferences prefs_; bool fresh_ = false;
};
}
