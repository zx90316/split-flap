// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cJSON.h>
#include <math.h>
#include <stdlib.h>
#include "display_model.h"

namespace cam {
inline cJSON* parseBody(const char* data, size_t length) {
  if (!data || !length || length > 8192 || memchr(data, 0, length) || strstr(data, "\\u0000")) return nullptr;
  uint8_t depth = 0; bool quoted = false, escaped = false;
  for (size_t i = 0; i < length; ++i) {
    const char c = data[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') quoted = true;
    else if (c == '{' || c == '[') { if (++depth > 16) return nullptr; }
    else if (c == '}' || c == ']') { if (!depth) return nullptr; --depth; }
  }
  const char* end = nullptr;
  cJSON* body = cJSON_ParseWithLengthOpts(data, length + 1, &end, true);
  if (!cJSON_IsObject(body) || end != data + length) { cJSON_Delete(body); return nullptr; }
  return body;
}
inline void str(cJSON* o, const char* key, const char* value) { cJSON_AddStringToObject(o, key, value); }
inline void num(cJSON* o, const char* key, double value) { cJSON_AddNumberToObject(o, key, value); }
inline void flag(cJSON* o, const char* key, bool value) { cJSON_AddBoolToObject(o, key, value); }
inline bool integer(const cJSON* p, double min, double max, int64_t& out) {
  if (!cJSON_IsNumber(p) || !isfinite(p->valuedouble) || floor(p->valuedouble) != p->valuedouble || p->valuedouble < min || p->valuedouble > max) return false;
  out = int64_t(p->valuedouble); return true;
}
inline bool fieldInteger(const cJSON* o, const char* key, double min, double max, int64_t& out) {
  return integer(cJSON_GetObjectItemCaseSensitive(o, key), min, max, out);
}
inline const char* stringField(const cJSON* o, const char* key) {
  const cJSON* p = cJSON_GetObjectItemCaseSensitive(o, key); return cJSON_IsString(p) ? p->valuestring : nullptr;
}
inline bool copyString(const char* input, char* output, size_t size) {
  if (!input || strlen(input) >= size) return false;
  memcpy(output, input, strlen(input) + 1); return true;
}
inline cJSON* settingsJson(const sf::Settings& settings) {
  cJSON* o = cJSON_CreateObject(); cJSON* map = cJSON_AddArrayToObject(o, "units");
  for (uint8_t i = 0; i < settings.count; ++i) cJSON_AddItemToArray(map, cJSON_CreateNumber(settings.mapping[i]));
  cJSON* configs = cJSON_AddArrayToObject(o, "calibration");
  for (const auto& c : settings.units) {
    cJSON* u = cJSON_CreateObject(); num(u, "revolution", c.revolution); num(u, "offset", c.offset);
    num(u, "speed", c.speed); num(u, "accel", c.accel); num(u, "direction", c.direction); flag(u, "activeLow", c.activeLow);
    char order[5]; for (uint8_t j = 0; j < 4; ++j) order[j] = char('1' + c.order[j]); order[4] = 0;
    str(u, "order", order); cJSON_AddItemToArray(configs, u);
  }
  return o;
}
inline bool parseSettings(const cJSON* o, sf::Settings& out) {
  const cJSON* units = cJSON_GetObjectItemCaseSensitive(o, "units"); const cJSON* cal = cJSON_GetObjectItemCaseSensitive(o, "calibration");
  if (!cJSON_IsArray(units) || cJSON_GetArraySize(units) < 1 || cJSON_GetArraySize(units) > 10 || !cJSON_IsArray(cal) || cJSON_GetArraySize(cal) != 10) return false;
  sf::Settings result; result.count = uint8_t(cJSON_GetArraySize(units)); int64_t n;
  for (uint8_t i = 0; i < result.count; ++i) { if (!integer(cJSON_GetArrayItem(units, i), 0, 9, n)) return false; result.mapping[i] = uint8_t(n); }
  for (uint8_t i = 0; i < 10; ++i) {
    const cJSON* item = cJSON_GetArrayItem(cal, i); sf::Calibration& c = result.units[i];
    if (!fieldInteger(item, "revolution", 1000, 20000, n)) return false; c.revolution = uint16_t(n);
    if (!fieldInteger(item, "offset", 0, c.revolution - 1, n)) return false; c.offset = uint16_t(n);
    if (!fieldInteger(item, "speed", 800, 30000, n)) return false; c.speed = uint16_t(n);
    if (!fieldInteger(item, "accel", 100, 6000, n)) return false; c.accel = uint16_t(n);
    if (!fieldInteger(item, "direction", -1, 1, n) || n == 0) return false; c.direction = int8_t(n);
    const cJSON* polarity = cJSON_GetObjectItemCaseSensitive(item, "activeLow");
    if (!cJSON_IsBool(polarity)) return false; c.activeLow = cJSON_IsTrue(polarity);
    const char* order = stringField(item, "order"); if (!order || strlen(order) != 4) return false;
    for (uint8_t j = 0; j < 4; ++j) c.order[j] = uint8_t(order[j] - '1');
  }
  if (!sf::validSettings(result)) return false;
  out = result; return true;
}
inline bool commandPayload(const cJSON* body, uint8_t width, Align align, uint8_t* out, uint8_t& length) {
  const char* name = stringField(body, "op"); if (!name) return false;
  struct Mapping { const char* name; sf::Op op; };
  static const Mapping names[] = {{"STOP", sf::Op::Stop}, {"OFF", sf::Op::Stop}, {"ARM", sf::Op::Arm},
    {"SET", sf::Op::SetIndex}, {"SETINDEX", sf::Op::SetIndex}, {"SHOW", sf::Op::Goto}, {"GOTO", sf::Op::Goto},
    {"SETTEXT", sf::Op::SetText}, {"TEXT", sf::Op::Text}, {"STEP", sf::Op::Step}, {"REV", sf::Op::Rev},
    {"FLAP", sf::Op::Flap}, {"HOME", sf::Op::Home}, {"MEASURE", sf::Op::Measure}, {"CYCLE", sf::Op::Cycle},
    {"LED", sf::Op::Led}, {"WATCH", sf::Op::Watch}, {"UNIT", sf::Op::Select}, {"DIGITS", sf::Op::Digits},
    {"UNITS", sf::Op::Units}, {"SPEED", sf::Op::Speed}, {"ACCEL", sf::Op::Accel}, {"REVSTEPS", sf::Op::Revolution},
    {"OFFSET", sf::Op::Offset}, {"DIR", sf::Op::Direction}, {"ORDER", sf::Op::Order}, {"POLARITY", sf::Op::Polarity},
    {"SAVE", sf::Op::Save}, {"LOAD", sf::Op::Load}, {"CLAIM", sf::Op::Claim}, {"INIT", sf::Op::Init}};
  uint8_t code = 0; for (const auto& entry : names) if (!strcmp(name, entry.name)) { code = uint8_t(entry.op); break; }
  if (!code) return false;
  const cJSON* target = cJSON_GetObjectItemCaseSensitive(body, "unit"); int64_t n;
  uint8_t unit = sf::kAll;
  if (cJSON_IsNumber(target)) { if (!integer(target, 0, 9, n)) return false; unit = uint8_t(n); }
  else if (target && (!cJSON_IsString(target) || strcmp(target->valuestring, "ALL"))) return false;
  out[0] = code; out[1] = unit; length = 2;
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(body, "value");
  const sf::Op op = sf::Op(code);
  if (op == sf::Op::SetText || op == sf::Op::Text) {
    if (!cJSON_IsString(value) || strlen(value->valuestring) > 80 || !textIndices(value->valuestring, width, align, op == sf::Op::SetText, out + 3)) return false;
    out[2] = width; length = width + 3;
  } else if (op == sf::Op::SetIndex || op == sf::Op::Goto) {
    if (cJSON_IsString(value)) { if (!textIndices(value->valuestring, 1, Align::Left, true, out + 2)) return false; }
    else { if (!integer(value, 0, 44, n)) return false; out[2] = uint8_t(n); }
    length = 3;
  } else if (op == sf::Op::Step || op == sf::Op::Speed || op == sf::Op::Accel || op == sf::Op::Revolution || op == sf::Op::Offset) {
    if (!integer(value, 0, 30000, n)) return false; sf::put16(out + 2, uint16_t(n)); length = 4;
  } else if (op == sf::Op::Direction) {
    if (!integer(value, -1, 1, n) || n == 0) return false; out[2] = uint8_t(int8_t(n)); length = 3;
  } else if (op == sf::Op::Order) {
    if (!cJSON_IsString(value) || strlen(value->valuestring) != 4) return false;
    for (uint8_t i = 0; i < 4; ++i) out[2 + i] = uint8_t(value->valuestring[i] - '1'); length = 6;
  } else if (op == sf::Op::Polarity) {
    if (!cJSON_IsString(value) || (strcmp(value->valuestring, "LOW") && strcmp(value->valuestring, "HIGH"))) return false;
    out[2] = !strcmp(value->valuestring, "LOW"); length = 3;
  } else if (op == sf::Op::Digits) {
    if (!integer(value, 1, 10, n)) return false; out[2] = uint8_t(n); length = 3;
  } else if (op == sf::Op::Units) {
    if (!cJSON_IsArray(value) || cJSON_GetArraySize(value) < 1 || cJSON_GetArraySize(value) > 10) return false;
    out[2] = uint8_t(cJSON_GetArraySize(value));
    for (uint8_t i = 0; i < out[2]; ++i) { if (!integer(cJSON_GetArrayItem(value, i), 0, 9, n)) return false; out[3 + i] = uint8_t(n); }
    length = 3 + out[2];
  }
  return true;
}
inline bool consolePayload(const char* line, uint8_t width, uint8_t selected, uint8_t* out, uint8_t& size) {
  if (!line || strlen(line) >= 96) return false;
  char buffer[96]; strcpy(buffer, line); char* rest = strchr(buffer, ' ');
  if (rest) { *rest++ = 0; while (*rest == ' ') ++rest; }
  else rest = buffer + strlen(buffer);
  cJSON* b = cJSON_CreateObject(); if (!b) return false;
  str(b, "op", buffer); num(b, "unit", selected);
  const bool text = !strcmp(buffer, "TEXT") || !strcmp(buffer, "SETTEXT");
  const bool string = text || !strcmp(buffer, "SET") || !strcmp(buffer, "SHOW") || !strcmp(buffer, "ORDER") || !strcmp(buffer, "POLARITY");
  if (text || !strcmp(buffer, "STOP") || !strcmp(buffer, "OFF") || !strcmp(buffer, "INIT") || !strcmp(buffer, "SAVE") || !strcmp(buffer, "LOAD")) {
    cJSON_DeleteItemFromObjectCaseSensitive(b, "unit"); str(b, "unit", "ALL");
  }
  if (!strncmp(rest, "ALL", 3) && (!rest[3] || rest[3] == ' ')) {
    cJSON_DeleteItemFromObjectCaseSensitive(b, "unit"); str(b, "unit", "ALL"); rest += 3; while (*rest == ' ') ++rest;
  }
  bool ok = true;
  if (!strcmp(buffer, "UNITS")) {
    cJSON* units = cJSON_AddArrayToObject(b, "value"); const char* p = rest;
    do {
      if (*p < '0' || *p > '9') { ok = false; break; }
      cJSON_AddItemToArray(units, cJSON_CreateNumber(*p++ - '0'));
      if (*p && *p++ != ',') { ok = false; break; }
      if (p[-1] == ',' && !*p) { ok = false; break; }
    } while (*p);
  } else if (string) {
    const size_t n = strlen(rest);
    if (n >= 2 && rest[0] == '"' && rest[n - 1] == '"') { rest[n - 1] = 0; ++rest; }
    str(b, "value", rest);
  } else if (*rest) {
    const bool numeric = !strcmp(buffer,"UNIT") || !strcmp(buffer,"STEP") || !strcmp(buffer,"SPEED") ||
      !strcmp(buffer,"ACCEL") || !strcmp(buffer,"REVSTEPS") || !strcmp(buffer,"OFFSET") ||
      !strcmp(buffer,"DIR") || !strcmp(buffer,"DIGITS") || !strcmp(buffer,"GOTO") || !strcmp(buffer,"SETINDEX");
    if (!numeric) { cJSON_Delete(b); return false; }
    char* end = nullptr; const long n = strtol(rest, &end, 10); ok = end != rest && !*end;
    if (!strcmp(buffer, "UNIT")) { cJSON_DeleteItemFromObjectCaseSensitive(b, "unit"); num(b, "unit", n); }
    else num(b, "value", n);
  }
  if (ok) ok = commandPayload(b, width, Align::Left, out, size);
  cJSON_Delete(b); return ok;
}
}
