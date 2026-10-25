// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "display_model.h"

namespace cam {
struct AppSettings {
  uint32_t version = 1;
  char ssid[33] = {}, wifiPassword[65] = {}, apPassword[64] = {}, adminPassword[64] = {};
  char mode[8] = "text", clockFormat[8] = "HH:MM", dateFormat[16] = "YYYY-MM-DD";
  int16_t timezone = 480;
  Align align = Align::Left;
};
inline void randomPassword(char* out, size_t length) {
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  for (size_t i = 0; i < length; ++i) out[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
  out[length] = 0;
}
inline bool validAppSettings(const AppSettings& s) {
  return s.version == 1 && s.ssid[32] == 0 && s.wifiPassword[64] == 0 && s.apPassword[63] == 0 && s.adminPassword[63] == 0 &&
      strlen(s.apPassword) >= 12 && strlen(s.adminPassword) >= 12 && s.mode[7] == 0 && s.clockFormat[7] == 0 && s.dateFormat[15] == 0 &&
      (!strcmp(s.mode, "text") || !strcmp(s.mode, "clock") || !strcmp(s.mode, "date")) &&
      validFormat("clock", s.clockFormat, 10) && validFormat("date", s.dateFormat, 10) &&
      uint8_t(s.align) <= 2 && s.timezone >= -720 && s.timezone <= 840 && s.timezone % 15 == 0;
}
class AppStore {
 public:
  bool begin(AppSettings& s) {
    if (!prefs_.begin("flap-cam", false)) return false;
    uint8_t data[sizeof(AppSettings) + 2];
    if (prefs_.getBytesLength("settings") == sizeof(data) && prefs_.getBytes("settings", data, sizeof(data)) == sizeof(data) &&
        sf::get16(data + sizeof(s)) == sf::crc16(data, sizeof(s))) {
      AppSettings saved; memcpy(&saved, data, sizeof(saved));
      if (validAppSettings(saved)) { s = saved; return true; }
    }
    randomPassword(s.apPassword, 12); randomPassword(s.adminPassword, 12); fresh_ = true;
    return save(s);
  }
  bool save(const AppSettings& s) {
    if (!validAppSettings(s)) return false;
    uint8_t data[sizeof(s) + 2]; memcpy(data, &s, sizeof(s)); sf::put16(data + sizeof(s), sf::crc16(data, sizeof(s)));
    return prefs_.putBytes("settings", data, sizeof(data)) == sizeof(data);
  }
  bool fresh() const { return fresh_; }
 private:
  Preferences prefs_; bool fresh_ = false;
};
}
