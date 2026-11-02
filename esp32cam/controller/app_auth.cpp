// SPDX-License-Identifier: GPL-3.0-only
#include "app.h"
#include <mbedtls/sha256.h>
namespace cam {
static bool digestToken(const String& cookie, uint8_t* out, String* raw = nullptr) {
  int start = 0;
  while (start < int(cookie.length())) {
    int end = cookie.indexOf(';', start); if (end < 0) end = cookie.length();
    String part = cookie.substring(start, end); part.trim();
    if (part.startsWith("SFSession=")) {
      const String value = part.substring(10); if (value.length() != 32) return false;
      for (size_t i = 0; i < value.length(); ++i) if (!isalnum(uint8_t(value[i]))) return false;
      if (raw) *raw = value;
      return mbedtls_sha256(reinterpret_cast<const uint8_t*>(value.c_str()), value.length(), out, 0) == 0;
    }
    start = end + 1;
  }
  return false;
}
bool App::authenticated(uint8_t* digest) {
  uint8_t hash[32]; if (!digestToken(server_.header("Cookie"), hash)) return false;
  if (digest) memcpy(digest, hash, 32);
  return sessions_.contains(hash);
}
bool App::auth() {
  if (authenticated()) return true;
  error(401, "login_required"); return false;
}
bool App::saveSessions(const SessionTable& candidate) {
  uint8_t data[SessionTable::kSize]; candidate.encode(data);
  if (!store_.writeSessions(data, sizeof(data))) return false;
  sessions_ = candidate; return true;
}
void App::session() {
  cJSON* result = cJSON_CreateObject(); const bool ok = authenticated(); flag(result, "authenticated", ok);
  if (ok) {
    uint8_t hash[32]; String raw; digestToken(server_.header("Cookie"), hash, &raw);
    server_.sendHeader("Set-Cookie", "SFSession=" + raw + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=34560000");
    str(result, "csrf", csrf_.c_str());
  }
  json(result);
}
void App::login() {
  const String origin = server_.header("Origin");
  if (origin.length() && origin != "http://" + server_.hostHeader()) { error(403, "origin"); return; }
  if (!server_.header("Content-Type").startsWith("application/json")) { error(415, "請使用登入表單。"); return; }
  if (loginRetryAt_ && int32_t(millis() - loginRetryAt_) < 0) { error(429, "嘗試次數過多，請稍後一分鐘再登入。"); return; }
  cJSON* b = body(); if (!b) return;
  const char* user = stringField(b, "username"), *password = stringField(b, "password");
  bool ok = user && !strcmp(user, "admin") && password && strlen(password) == strlen(settings_.adminPassword);
  uint8_t difference = 0; if (ok) for (size_t i = 0; i < strlen(password); ++i) difference |= password[i] ^ settings_.adminPassword[i];
  ok = ok && !difference; cJSON_Delete(b);
  if (!ok) { if (++loginFailures_ >= 5) { loginFailures_ = 0; loginRetryAt_ = millis() + 60000; } error(401, "帳號或密碼不正確。"); return; }
  char value[33]; randomPassword(value, 32); uint8_t hash[32];
  if (mbedtls_sha256(reinterpret_cast<uint8_t*>(value), 32, hash, 0)) { error(500, "無法建立登入。"); return; }
  SessionTable candidate = sessions_; candidate.add(hash);
  if (!saveSessions(candidate)) { error(500, "登入記錄保存失敗。"); return; }
  loginFailures_ = 0; loginRetryAt_ = 0;
  server_.sendHeader("Set-Cookie", String("SFSession=") + value + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=34560000");
  cJSON* result = cJSON_CreateObject(); flag(result, "authenticated", true); str(result, "csrf", csrf_.c_str()); json(result);
}
void App::logout() {
  if (!control()) return;
  uint8_t hash[32]; authenticated(hash); SessionTable candidate = sessions_; candidate.remove(hash);
  if (!saveSessions(candidate)) { error(500, "登出保存失敗，請重試。"); return; }
  server_.sendHeader("Set-Cookie", "SFSession=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0"); json(cJSON_CreateObject());
}
}
