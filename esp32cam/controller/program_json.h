// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "app_settings.h"
#include "json_helpers.h"
namespace cam {
inline bool parseProgram(const cJSON* p, AppSettings& s) {
  if (!cJSON_IsObject(p)) return false;
  const auto string = [p](const char* key, char* out, size_t n) {
    return !cJSON_GetObjectItemCaseSensitive(p, key) || copyString(stringField(p, key), out, n);
  };
  const auto boolean = [p](const char* key, bool& value) {
    const cJSON* b = cJSON_GetObjectItemCaseSensitive(p, key); if (!b) return true;
    if (!cJSON_IsBool(b)) return false; value = cJSON_IsTrue(b); return true;
  };
  if (!string("mode", s.mode, sizeof(s.mode)) || !string("clockFormat", s.clockFormat, sizeof(s.clockFormat)) ||
      !string("dateFormat", s.dateFormat, sizeof(s.dateFormat)) || !string("weekdayFormat", s.weekdayFormat, sizeof(s.weekdayFormat)) ||
      !boolean("leadingZero", s.leadingZero) || !boolean("limited", s.limited) || !boolean("autoRecovery", s.autoRecovery)) return false;
  int64_t n;
  if (cJSON_GetObjectItemCaseSensitive(p, "timezone")) { if (!fieldInteger(p, "timezone", -720, 840, n)) return false; s.timezone = int16_t(n); }
  if (cJSON_GetObjectItemCaseSensitive(p, "align")) { if (!fieldInteger(p, "align", 0, 2, n)) return false; s.align = Align(n); }
  if (cJSON_GetObjectItemCaseSensitive(p, "windowStart")) { if (!fieldInteger(p, "windowStart", 0, 1439, n)) return false; s.windowStart = uint16_t(n); }
  if (cJSON_GetObjectItemCaseSensitive(p, "windowEnd")) { if (!fieldInteger(p, "windowEnd", 0, 1439, n)) return false; s.windowEnd = uint16_t(n); }
  const cJSON* items = cJSON_GetObjectItemCaseSensitive(p, "items");
  if (items) {
    if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) < 1 || cJSON_GetArraySize(items) > 8) return false;
    s.itemCount = cJSON_GetArraySize(items); s.itemIndex = 0;
    for (uint8_t i = 0; i < s.itemCount; ++i) {
      const cJSON* item = cJSON_GetArrayItem(items, i); DisplayItem d;
      if (!copyString(stringField(item, "mode"), d.mode, sizeof(d.mode)) || !copyString(stringField(item, "format"), d.format, sizeof(d.format)) ||
          !copyString(stringField(item, "text"), d.text, sizeof(d.text)) || !fieldInteger(item, "seconds", 10, 3600, n)) return false;
      d.seconds = uint16_t(n); if (!fieldInteger(item, "align", 0, 2, n)) return false; d.align = Align(n);
      if (!validItem(d, 10)) return false; s.items[i] = d;
    }
  }
  return validAppSettings(s);
}
inline cJSON* programJson(const AppSettings& s) {
  cJSON* p = cJSON_CreateObject(); str(p, "mode", s.mode); str(p, "clockFormat", s.clockFormat); str(p, "dateFormat", s.dateFormat);
  str(p, "weekdayFormat", s.weekdayFormat); num(p, "timezone", s.timezone); num(p, "align", uint8_t(s.align));
  flag(p, "leadingZero", s.leadingZero); flag(p, "limited", s.limited); flag(p, "autoRecovery", s.autoRecovery);
  num(p, "windowStart", s.windowStart); num(p, "windowEnd", s.windowEnd); num(p, "itemIndex", s.itemIndex);
  cJSON* items = cJSON_AddArrayToObject(p, "items");
  for (uint8_t i = 0; i < s.itemCount; ++i) {
    const auto& d = s.items[i]; cJSON* row = cJSON_CreateObject(); str(row, "mode", d.mode); str(row, "format", d.format); str(row, "text", d.text);
    num(row, "seconds", d.seconds); num(row, "align", uint8_t(d.align)); cJSON_AddItemToArray(items, row);
  }
  return p;
}
inline bool programFits(const AppSettings& s, uint8_t width) {
  if (!strcmp(s.mode, "text")) return true;
  if (!strcmp(s.mode, "carousel")) {
    for (uint8_t i = 0; i < s.itemCount; ++i) if (!validItem(s.items[i], width)) return false;
    return true;
  }
  return validFormat(s.mode, !strcmp(s.mode, "clock") ? s.clockFormat : !strcmp(s.mode, "date") ? s.dateFormat : s.weekdayFormat, width);
}
}
