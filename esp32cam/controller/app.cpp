// SPDX-License-Identifier: GPL-3.0-only
#include "app.h"
#include "web_assets.h"
#include <esp_app_format.h>
#include <esp_sntp.h>
#include <sys/time.h>

namespace cam {
static volatile bool ntpReceived = false;
static void ntpCallback(struct timeval*) { ntpReceived = true; }
static String token() { char value[33]; randomPassword(value, 32); return String(value); }
static bool same(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t difference = 0; for (size_t i = 0; i < a.length(); ++i) difference |= uint8_t(a[i] ^ b[i]);
  return difference == 0;
}
static const char* stageName(uint8_t stage) {
  switch (stage) { case 0: return "queued"; case 1: return "accepted"; case 2: return "done"; case 3: return "rejected"; default: return "fault"; }
}
void App::begin() {
  LocalController::bootReset();
  Serial.begin(115200);
  // AI-Thinker only: power down the unused camera and keep its flash LED off.
  gpio_set_level(GPIO_NUM_32, 1); pinMode(32, OUTPUT); digitalWrite(32, HIGH);
  gpio_set_level(GPIO_NUM_4, 0); pinMode(4, OUTPUT); digitalWrite(4, LOW);
  // Enable the RF entropy source before generating per-device passwords and session nonces.
  WiFi.persistent(false); WiFi.setHostname("splitflap"); WiFi.mode(WIFI_AP_STA); WiFi.setAutoReconnect(true);
  storageOk_ = store_.begin(settings_);
  if (storageOk_) store_.loadDisplayMemory(displayMemory_);
  if (!validAppSettings(settings_)) { randomPassword(settings_.apPassword, 12); randomPassword(settings_.adminPassword, 12); }
  csrf_ = token(); apSsid_ = "SplitFlap-" + String(uint32_t(ESP.getEfuseMac() >> 24) & 0xffffff, HEX);
  controller_.begin();
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid_.c_str(), settings_.apPassword, 1, 0, 4);
  dns_.start(53, "*", WiFi.softAPIP());
  if (settings_.ssid[0]) WiFi.begin(settings_.ssid, settings_.wifiPassword);
  MDNS.begin("splitflap"); MDNS.addService("http", "tcp", 80);
  sntp_set_time_sync_notification_cb(ntpCallback); configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  const char* headers[] = {"X-CSRF-Token", "Origin", "X-OTA-Ticket"}; server_.collectHeaders(headers, 3);
  routes(); server_.begin(); mbedtls_md_init(&hash_);
  Serial.println("Split-Flap ESP32-CAM MCP 2.0.0. SDA=13 SCL=14 RST=15; 400kHz. Motors OFF.");
  Serial.println("ACCESS: show device Wi-Fi and admin credentials. STATUS | NETWORK RESET | HELP");
  if (store_.fresh() || !storageOk_) printAccess();
  if (!storageOk_) Serial.println("FAULT: NVS unavailable; settings are not durable.");
}
void App::pause(const char* reason) { running_ = false; automaticJob_ = 0; strncpy(pauseReason_, reason, sizeof(pauseReason_) - 1); pauseReason_[sizeof(pauseReason_) - 1] = 0; }
void App::tick() {
  rememberDisplay();
  server_.handleClient(); dns_.processNextRequest(); console();
  if (ntpReceived) { ntpReceived = false; timeSource_ = "ntp"; }
  if (networkAt_ && int32_t(millis() - networkAt_) >= 0) { networkAt_ = 0; connectNetwork(); }
  if (restartAt_ && int32_t(millis() - restartAt_) >= 0) ESP.restart();
  if (pendingSettingsJob_) {
    const State s = controller_.snapshot();
    for (const auto& job : s.jobs) if (job.id == pendingSettingsJob_ && job.stage >= uint8_t(sf::Stage::Done)) {
      if (job.stage == uint8_t(sf::Stage::Done)) {
        if (writeAppSettings(pendingSettings_)) { settings_ = pendingSettings_; storageOk_ = true; }
        else {
          storageOk_ = false; controller_.configurationFailed(pendingSettingsJob_);
          pause("安裝配置已保存；顯示偏好保存失敗，請重新匯入。");
        }
      } else pause("控制器拒絕匯入；顯示偏好未更改。");
      pendingSettingsJob_ = 0; break;
    }
  }
  if (resetRequested_) {
    const State s = controller_.snapshot(); bool stopped = false;
    for (const auto& j : s.jobs) if (j.id == resetStopJob_ && j.stage == uint8_t(sf::Stage::Done)) stopped = true;
    if (stopped || (!s.connected && uint32_t(millis() - resetAt_) > 2500)) { resetRequested_ = false; resetNetwork(); }
  }
  scheduler(); delay(1);
}
void App::rememberDisplay() {
  const State s = controller_.snapshot(); const uint32_t now = millis();
  if (!s.connected || !s.valid || uint32_t(now - s.updatedAt) > 2000 ||
      s.panel.busy || s.panel.saving || s.panel.watching || s.maintenance || controller_.pending()) return;
  if (!displayMemory_.observe(s.units)) return;
  if (displayMemoryRetryAt_ && int32_t(now - displayMemoryRetryAt_) < 0) return;
  if (!controller_.flash(true)) return;
  displayMemoryOk_ = store_.saveDisplayMemory(displayMemory_);
  controller_.flash(false);
  if (displayMemoryOk_) { displayMemory_.saved(); displayMemoryRetryAt_ = 0; }
  else displayMemoryRetryAt_ = now + 5000;
}
bool App::auth() {
  if (server_.authenticate("admin", settings_.adminPassword)) return true;
  server_.requestAuthentication(DIGEST_AUTH, "SplitFlap", "請用 admin 與串列 ACCESS 顯示的密碼登入。"); return false;
}
bool App::csrfMatches() const {
  if (!same(server_.header("X-CSRF-Token"), csrf_)) return false;
  const String origin = server_.header("Origin");
  return !origin.length() || origin == "http://" + server_.hostHeader();
}
bool App::control() {
  if (!auth()) return false;
  if (!csrfMatches()) { error(403, "請重新載入頁面後操作（安全驗證失效）。"); return false; }
  return true;
}
void App::json(cJSON* object, int code) {
  if (!object) { server_.send(503, "application/json", "{\"error\":\"memory\"}"); return; }
  char* encoded = cJSON_PrintUnformatted(object); cJSON_Delete(object);
  server_.sendHeader("Cache-Control", "no-store"); server_.sendHeader("X-Content-Type-Options", "nosniff");
  if (!encoded) { server_.send(503, "application/json", "{\"error\":\"memory\"}"); return; }
  server_.send(code, "application/json; charset=utf-8", encoded); cJSON_free(encoded);
}
void App::error(int code, const char* message) { cJSON* o = cJSON_CreateObject(); str(o, "error", message); json(o, code); }
cJSON* App::body() {
  const String data = server_.arg("plain");
  if (!data.length() || data.length() > 8192) { error(400, "JSON 內容空白或過長。"); return nullptr; }
  cJSON* o = parseBody(data.c_str(), data.length());
  if (!cJSON_IsObject(o)) { cJSON_Delete(o); error(400, "JSON 格式錯誤。"); return nullptr; }
  return o;
}
bool App::requestId(const cJSON* b, const State& s, uint32_t& request) {
  int64_t boot, id;
  if (!fieldInteger(b, "boot", 1, 4294967295.0, boot) || uint32_t(boot) != s.boot || !fieldInteger(b, "request", 1, 4294967295.0, id)) {
    error(409, "設備已重新啟動或命令編號無效；請重新載入，不會重送舊動作。"); return false;
  }
  request = uint32_t(id); return true;
}
bool App::idle(const State& s) {
  if (pendingSettingsJob_ || s.maintenance || s.panel.busy || s.panel.saving || s.panel.watching || controller_.pending()) {
    error(409, "請先停止或等待目前工作完成。"); return false;
  }
  return true;
}
bool App::saveApp(const AppSettings& candidate) {
  if (!writeAppSettings(candidate)) { error(500, "設定保存失敗或控制器忙碌；原設定保留。"); return false; }
  settings_ = candidate; storageOk_ = true; return true;
}
bool App::writeAppSettings(const AppSettings& candidate) {
  if (!controller_.flash(true)) return false;
  const bool ok = store_.save(candidate); controller_.flash(false); return ok;
}
void App::routes() {
  const auto wiring = [this](const uint8_t* data, size_t size) {
    if (!auth()) return;
    server_.sendHeader("Content-Encoding", "gzip"); server_.sendHeader("Cache-Control", "no-cache");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.send_P(200, "image/svg+xml; charset=utf-8", reinterpret_cast<const char*>(data), size);
  };
  server_.on("/wiring-runtime.svg", HTTP_GET, [wiring]() { wiring(kWiringRuntimeGzip, sizeof(kWiringRuntimeGzip)); });
  server_.on("/wiring-flashing.svg", HTTP_GET, [wiring]() { wiring(kWiringFlashingGzip, sizeof(kWiringFlashingGzip)); });
  server_.on("/wiring-motors.svg", HTTP_GET, [wiring]() { wiring(kWiringMCPGzip, sizeof(kWiringMCPGzip)); });
  server_.on("/wiring-hall.svg", HTTP_GET, [wiring]() { wiring(kWiringHallGzip, sizeof(kWiringHallGzip)); });
  server_.on("/", HTTP_GET, [this]() {
    if (!auth()) return;
    server_.sendHeader("Content-Encoding", "gzip"); server_.sendHeader("Cache-Control", "no-cache");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.sendHeader("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; form-action 'self'");
    server_.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(kWebGzip), sizeof(kWebGzip));
  });
  server_.on("/api/state", HTTP_GET, [this]() { if (auth()) state(); });
  server_.on("/api/config", HTTP_GET, [this]() { if (auth()) getConfiguration(); });
  server_.on("/api/command", HTTP_POST, [this]() { if (control()) command(); });
  server_.on("/api/config", HTTP_POST, [this]() { if (control()) configuration(); });
  server_.on("/api/network", HTTP_POST, [this]() { if (control()) network(); });
  server_.on("/api/preferences", HTTP_POST, [this]() { if (control()) preferences(); });
  server_.on("/api/time", HTTP_POST, [this]() { if (control()) setTime(); });
  server_.on("/api/ota/prepare", HTTP_POST, [this]() { if (control()) prepareOta(); });
  server_.on("/api/update", HTTP_POST, [this]() { finishOta(); }, [this]() { uploadOta(); });
  server_.onNotFound([this]() {
    if (server_.uri().startsWith("/api/")) { if (auth()) error(404, "找不到此功能。"); return; }
    server_.sendHeader("Location", "http://192.168.4.1/"); server_.send(302, "text/plain", "Open SplitFlap");
  });
}
void App::command() {
  cJSON* b = body(); if (!b) return;
  const State s = controller_.snapshot(); uint32_t request = 0; uint8_t bytes[sf::kMaxPayload], n = 0;
  if (!requestId(b, s, request)) { cJSON_Delete(b); return; }
  Align align = settings_.align; int64_t alignment;
  const cJSON* field = cJSON_GetObjectItemCaseSensitive(b, "align");
  if (field) { if (!integer(field, 0, 2, alignment)) { cJSON_Delete(b); error(400, "對齊方式無效。"); return; } align = Align(alignment); }
  const bool ok = commandPayload(b, s.settings.count, align, bytes, n); cJSON_Delete(b);
  if (!ok) { error(400, "命令、字元、長度或參數無效；SETTEXT 必須等於目前位數。"); return; }
  pause("已切換為手動操作。"); uint32_t id = 0; const sf::Error result = controller_.submit(bytes, n, request, id);
  if (result != sf::Error::None) { error(409, sf::errorName(result)); return; }
  cJSON* response = cJSON_CreateObject(); num(response, "id", id); json(response, 202);
}
void App::configuration() {
  cJSON* b = body(); if (!b) return;
  const State s = controller_.snapshot(); uint32_t request = 0; sf::Settings config;
  if (!requestId(b, s, request)) { cJSON_Delete(b); return; }
  int64_t schema = 0;
  const char* device = stringField(b, "device");
  const cJSON* installation = cJSON_GetObjectItemCaseSensitive(b, "controller");
  if (!installation) installation = cJSON_GetObjectItemCaseSensitive(b, "mega"); // Import the dual-controller backup.
  const bool ok = (!device || !strcmp(device, "splitflap-esp32cam") || !strcmp(device, "splitflap-esp32cam-mcp")) &&
      fieldInteger(b, "schema", 1, 2, schema) && parseSettings(installation, config);
  bool hall = s.hallEnabled, hardwareOk = true;
  const cJSON* hardware = cJSON_GetObjectItemCaseSensitive(b, "hardware");
  if (hardware) {
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(hardware, "hallEnabled");
    hardwareOk = cJSON_IsObject(hardware) && cJSON_IsBool(enabled); hall = cJSON_IsTrue(enabled);
  }
  AppSettings candidate = settings_;
  const cJSON* prefs = cJSON_GetObjectItemCaseSensitive(b, "display"); const bool hasPrefs = prefs != nullptr; int64_t value;
  bool prefsOk = true;
  if (prefs) {
    prefsOk = fieldInteger(prefs, "timezone", -720, 840, value); if (prefsOk) candidate.timezone = int16_t(value);
    prefsOk = prefsOk && fieldInteger(prefs, "align", 0, 2, value); if (prefsOk) candidate.align = Align(value);
    prefsOk = prefsOk && copyString(stringField(prefs, "mode"), candidate.mode, sizeof(candidate.mode)) &&
      copyString(stringField(prefs, "clockFormat"), candidate.clockFormat, sizeof(candidate.clockFormat)) &&
      copyString(stringField(prefs, "dateFormat"), candidate.dateFormat, sizeof(candidate.dateFormat)) && validAppSettings(candidate);
  }
  cJSON_Delete(b);
  if (!ok || !prefsOk || !hardwareOk) { error(400, "整份設定驗證失敗；未套用。"); return; }
  if (!idle(s)) return;
  pause("設定已保存；速度調整保留目前字元與轉動許可。定位校準有變更時，請確認實際字元。");
  uint8_t bytes[sf::kSettingsSize + 3]; bytes[0] = uint8_t(sf::Op::ApplyConfig); bytes[1] = sf::kAll;
  sf::encodeSettings(config, bytes + 2); bytes[sf::kSettingsSize + 2] = hall; uint32_t id = 0;
  const sf::Error result = controller_.submit(bytes, sizeof(bytes), request, id);
  if (result != sf::Error::None) { error(409, sf::errorName(result)); return; }
  // Display preferences never contain movement or arming state.
  if (hasPrefs && request >= s.nextRequest) { pendingSettings_ = candidate; pendingSettingsJob_ = id; }
  cJSON* response = cJSON_CreateObject(); num(response, "id", id); json(response, 202);
}
void App::getConfiguration() {
  if (pendingSettingsJob_) { error(409, "設定正在保存，請完成後再匯出。"); return; }
  const State s = controller_.snapshot(); if (!s.valid) { error(503, "尚未讀到控制器設定。"); return; }
  cJSON* o = cJSON_CreateObject(); num(o, "schema", 2); str(o, "device", "splitflap-esp32cam-mcp");
  cJSON_AddItemToObject(o, "controller", settingsJson(s.settings));
  cJSON* hardware = cJSON_AddObjectToObject(o, "hardware"); flag(hardware, "hallEnabled", s.hallEnabled);
  cJSON* d = cJSON_AddObjectToObject(o, "display"); str(d, "mode", settings_.mode);
  str(d, "clockFormat", settings_.clockFormat); str(d, "dateFormat", settings_.dateFormat);
  num(d, "timezone", settings_.timezone); num(d, "align", uint8_t(settings_.align)); json(o);
}
void App::network() {
  const State s = controller_.snapshot();
  if (!canConfigureNetwork(s, controller_.pending(), pendingSettingsJob_ != 0)) {
    error(409, "請先停止或等待目前工作完成。"); return;
  }
  cJSON* b = body(); if (!b) return; AppSettings candidate = settings_;
  const char* ssid = stringField(b, "ssid"), *password = stringField(b, "password"), *apPassword = stringField(b, "apPassword"), *adminPassword = stringField(b, "adminPassword");
  bool ok = ssid && copyString(ssid, candidate.ssid, sizeof(candidate.ssid));
  if (password) ok = ok && copyString(password, candidate.wifiPassword, sizeof(candidate.wifiPassword));
  if (apPassword && *apPassword) ok = ok && copyString(apPassword, candidate.apPassword, sizeof(candidate.apPassword));
  if (adminPassword && *adminPassword) ok = ok && copyString(adminPassword, candidate.adminPassword, sizeof(candidate.adminPassword));
  ok = ok && validAppSettings(candidate); cJSON_Delete(b);
  if (!ok) { error(400, "網路資料無效；直連／登入密碼須為 12～63 字元。"); return; }
  pause("網路設定已修改，自動顯示已暫停。"); if (!saveApp(candidate)) return;
  cJSON* o = cJSON_CreateObject(); str(o, "message", "已保存。若更改直連密碼，請用新密碼重新連線；登入密碼變更後需重新登入。"); json(o);
  networkAt_ = millis() + 1200;
}
void App::connectNetwork() {
  WiFi.softAPdisconnect(false); WiFi.softAP(apSsid_.c_str(), settings_.apPassword, 1, 0, 4);
  WiFi.disconnect(false, false); if (settings_.ssid[0]) WiFi.begin(settings_.ssid, settings_.wifiPassword);
}
void App::preferences() {
  cJSON* b = body(); if (!b) return;
  const bool start = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(b, "run"));
  const char* mode = stringField(b, "mode");
  // Pausing preserves an accepted movement; STOP is the separate immediate stop control.
  if (!start && !mode) { cJSON_Delete(b); pause("自動顯示已暫停。"); json(cJSON_CreateObject()); return; }
  const State s = controller_.snapshot(); if (!idle(s)) { cJSON_Delete(b); return; }
  AppSettings candidate = settings_; int64_t n;
  bool ok = copyString(mode, candidate.mode, sizeof(candidate.mode));
  const char* clock = stringField(b, "clockFormat"), *date = stringField(b, "dateFormat");
  if (clock) ok = ok && copyString(clock, candidate.clockFormat, sizeof(candidate.clockFormat));
  if (date) ok = ok && copyString(date, candidate.dateFormat, sizeof(candidate.dateFormat));
  if (cJSON_GetObjectItemCaseSensitive(b, "timezone")) { ok = ok && fieldInteger(b, "timezone", -720, 840, n); if (ok) candidate.timezone = int16_t(n); }
  if (cJSON_GetObjectItemCaseSensitive(b, "align")) { ok = ok && fieldInteger(b, "align", 0, 2, n); if (ok) candidate.align = Align(n); }
  cJSON_Delete(b); ok = ok && validAppSettings(candidate);
  if (!ok) { error(400, "顯示設定無效。"); return; }
  if (start && strcmp(candidate.mode, "text")) {
    uint8_t indices[10];
    if (!s.connected || !s.valid || s.uncertain || !formatTime(time(nullptr), candidate.timezone, candidate.mode,
        !strcmp(candidate.mode, "clock") ? candidate.clockFormat : candidate.dateFormat, s.settings.count, candidate.align, indices)) {
      error(409, "尚未校時、位數不足，或 MCP 尚未就緒。"); return;
    }
    for (uint8_t i = 0; i < s.panel.count; ++i) {
      const sf::UnitState& u = s.units[s.panel.mapping[i]];
      if (!u.armed || !u.reference) { error(409, "請先設定每位目前字元或 HOME，然後 ARM ALL。"); return; }
    }
  }
  if (!saveApp(candidate)) return;
  running_ = start && strcmp(candidate.mode, "text"); lastUsbRevision_ = s.panel.usbRevision; automaticJob_ = 0;
  copyString(running_ ? "自動顯示執行中。" : "自動顯示已暫停。", pauseReason_, sizeof(pauseReason_));
  json(cJSON_CreateObject());
}
void App::setTime() {
  cJSON* b = body(); if (!b) return; int64_t seconds;
  const bool ok = fieldInteger(b, "unix", 1704067200.0, 4102444799.0, seconds); cJSON_Delete(b);
  if (!ok) { error(400, "時間必須介於 2024～2099 年。"); return; }
  timeval tv{}; tv.tv_sec = time_t(seconds); settimeofday(&tv, nullptr); timeSource_ = "phone"; json(cJSON_CreateObject());
}
void App::scheduler() {
  if (!running_ || uint32_t(millis() - scheduleAt_) < 200) return; scheduleAt_ = millis();
  const State s = controller_.snapshot();
  if (!s.connected || !s.valid || s.uncertain || s.panel.usbLocked || s.panel.usbRevision != lastUsbRevision_) {
    pause("連線或控制權已變更，請檢查狀態後手動啟動。"); return;
  }
  if (automaticJob_) {
    for (const auto& j : s.jobs) if (j.id == automaticJob_ && j.stage >= uint8_t(sf::Stage::Rejected)) { pause("顯示工作失敗，請檢查 MCP 狀態。"); return; }
  }
  if (s.panel.busy || s.panel.saving || controller_.pending()) return;
  uint8_t bytes[13] = {uint8_t(sf::Op::Text), sf::kAll, s.settings.count};
  if (!formatTime(time(nullptr), settings_.timezone, settings_.mode,
      !strcmp(settings_.mode, "clock") ? settings_.clockFormat : settings_.dateFormat,
      s.settings.count, settings_.align, bytes + 3)) { pause("時間或顯示格式無效。"); return; }
  bool changed = false;
  for (uint8_t i = 0; i < s.panel.count; ++i) {
    const sf::UnitState& u = s.units[s.panel.mapping[i]];
    if (!u.armed || !u.reference) { pause("定位或啟用狀態已失效，請重新定位與 ARM ALL。"); return; }
    if (u.index != bytes[3 + i]) changed = true;
  }
  if (!changed) return;
  const sf::Error result = controller_.submit(bytes, 3 + s.settings.count, 0, automaticJob_);
  if (result != sf::Error::None && result != sf::Error::Busy) pause("無法送出顯示工作，請檢查連線。");
}
bool App::otaReady(const State& s) const {
  if (!otaStopJob_ || !s.valid || !s.outputsDisabled || s.panel.busy || s.panel.saving || s.panel.watching || uint32_t(millis() - s.updatedAt) > 1500) return false;
  bool acknowledged = false;
  for (const auto& j : s.jobs) if (j.id == otaStopJob_ && j.stage == uint8_t(sf::Stage::Done)) acknowledged = true;
  for (uint8_t i = 0; i < s.panel.count; ++i) if (s.units[s.panel.mapping[i]].armed) return false;
  return acknowledged;
}
void App::prepareOta() {
  cJSON* b = body(); if (!b) return; const State s = controller_.snapshot(); uint32_t request = 0;
  if (!requestId(b, s, request)) { cJSON_Delete(b); return; } cJSON_Delete(b);
  pause("已準備更新 ESP32，更新後須重新定位與啟用。");
  const uint8_t stop[] = {uint8_t(sf::Op::Stop), sf::kAll};
  const sf::Error result = controller_.submit(stop, sizeof(stop), request, otaStopJob_);
  if (result != sf::Error::None) { error(409, sf::errorName(result)); return; }
  otaTicket_ = token(); otaError_ = ""; otaFileSeen_ = false; otaSucceeded_ = false;
  cJSON* o = cJSON_CreateObject(); num(o, "id", otaStopJob_); str(o, "ticket", otaTicket_.c_str()); json(o, 202);
}
void App::otaFailure(const char* reason) {
  otaError_ = reason; otaAuthorized_ = false; otaSucceeded_ = false;
  if (otaStarted_) { Update.abort(); otaStarted_ = false; }
  mbedtls_md_free(&hash_); mbedtls_md_init(&hash_); hashActive_ = false;
  controller_.maintenance(false); otaTicket_ = "";
}
void App::uploadOta() {
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (otaFileSeen_) { otaFailure("一次只能上傳一個韌體檔案，請重新準備更新。"); return; }
    otaFileSeen_ = true;
    otaSucceeded_ = false; otaError_ = ""; otaHeaderAt_ = imagePrefixAt_ = 0; otaSize_ = otaReceived_ = 0;
    const State s = controller_.snapshot();
    if (!server_.authenticate("admin", settings_.adminPassword) || !csrfMatches() || !otaTicket_.length() ||
        !same(server_.header("X-OTA-Ticket"), otaTicket_) || !otaReady(s) || !controller_.maintenance(true)) {
      otaFailure("更新未授權，或尚未取得 MCP 停止確認。"); return;
    }
    otaAuthorized_ = true;
    if (mbedtls_md_setup(&hash_, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) != 0 || mbedtls_md_starts(&hash_) != 0) {
      otaFailure("無法初始化映像校驗。"); return;
    }
    hashActive_ = true;
  } else if (upload.status == UPLOAD_FILE_WRITE && otaAuthorized_) {
    const State s = controller_.snapshot();
    if (!s.maintenance || !otaReady(s)) { otaFailure("更新期間 MCP 失聯或停止狀態已改變。"); return; }
    otaChunk(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END && otaAuthorized_) {
    const State s = controller_.snapshot();
    if (!s.maintenance || !otaReady(s)) { otaFailure("更新期間 MCP 失聯或停止狀態已改變。"); return; }
    uint8_t hash[32];
    if (!otaStarted_ || otaReceived_ != otaSize_ || mbedtls_md_finish(&hash_, hash) != 0 || memcmp(hash, otaHeader_ + 16, 32)) {
      otaFailure("映像長度或 SHA-256 不符，保留目前韌體。"); return;
    }
    mbedtls_md_free(&hash_); mbedtls_md_init(&hash_); hashActive_ = false;
    // Do not select the new partition until the complete HTTP request passes its final handler.
    otaSucceeded_ = true; otaAuthorized_ = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) otaFailure("上傳已中斷，可重新準備更新。");
}
void App::otaChunk(uint8_t* data, size_t size) {
  while (size && otaHeaderAt_ < sizeof(otaHeader_)) { otaHeader_[otaHeaderAt_++] = *data++; --size; }
  if (otaHeaderAt_ != sizeof(otaHeader_)) return;
  if (!otaSize_ && !otaHeader(otaHeader_, 1966080, otaSize_)) { otaFailure("只接受本專案的 .sffw 檔案或相容協定版本。"); return; }
  if (uint64_t(otaReceived_) + imagePrefixAt_ + size > otaSize_) { otaFailure("映像資料超出宣告長度。"); return; }
  if (!otaStarted_) {
    while (size && imagePrefixAt_ < sizeof(esp_image_header_t)) { imagePrefix_[imagePrefixAt_++] = *data++; --size; }
    if (imagePrefixAt_ < sizeof(esp_image_header_t)) return;
    esp_image_header_t image; memcpy(&image, imagePrefix_, sizeof(image));
    if (image.magic != ESP_IMAGE_HEADER_MAGIC || image.chip_id != ESP_CHIP_ID_ESP32 || !Update.begin(otaSize_, U_FLASH)) {
      otaFailure("映像晶片型號不符，或 OTA 空間不足。"); return;
    }
    otaStarted_ = true;
    if (mbedtls_md_update(&hash_, imagePrefix_, imagePrefixAt_) != 0 || Update.write(imagePrefix_, imagePrefixAt_) != imagePrefixAt_) {
      otaFailure("映像寫入失敗。"); return;
    }
    otaReceived_ += imagePrefixAt_; imagePrefixAt_ = 0;
  }
  if (size && (mbedtls_md_update(&hash_, data, size) != 0 || Update.write(data, size) != size)) { otaFailure("映像寫入失敗。"); return; }
  otaReceived_ += size;
}
void App::finishOta() {
  if (!control()) { otaFailure("更新請求驗證失敗。"); return; }
  if (!otaSucceeded_) { error(400, otaError_.length() ? otaError_.c_str() : "沒有有效的更新檔案。"); return; }
  const State s = controller_.snapshot();
  if (!s.maintenance || !otaReady(s)) { otaFailure("MCP 停止確認已失效，請重新準備更新。"); error(409, otaError_.c_str()); return; }
  if (!Update.end(true)) { otaFailure("ESP32 拒絕此映像，保留目前韌體。"); error(400, otaError_.c_str()); return; }
  otaStarted_ = false; otaTicket_ = "";
  cJSON* o = cJSON_CreateObject(); str(o, "message", "更新完成，即將重新啟動；不會自動轉動馬達。"); json(o);
  restartAt_ = millis() + 1200;
}
void App::state() {
  const State s = controller_.snapshot(); cJSON* o = cJSON_CreateObject();
  str(o, "firmware", "2.0.0"); str(o, "architecture", "esp32cam-mcp"); num(o, "protocol", 2);
  num(o, "boot", s.boot); num(o, "nextRequest", s.nextRequest); str(o, "csrf", csrf_.c_str());
  cJSON* hardware = cJSON_AddObjectToObject(o, "hardware");
  flag(hardware, "hallEnabled", s.hallEnabled); flag(hardware, "initialized", s.initialized); flag(hardware, "outputsDisabled", s.outputsDisabled);
  flag(hardware, "configStorageOk", s.configStorageOk); num(hardware, "presentMask", s.presentMask); num(hardware, "requiredMask", s.requiredMask);
  num(hardware, "clockHz", 400000); num(hardware, "maxTickGapUs", s.maxTickGapUs); num(hardware, "maxFlushUs", s.maxFlushUs); num(hardware, "lateTicks", s.lateTicks);
  flag(o, "displayMemoryOk", displayMemoryOk_);
  flag(o, "connected", s.connected && s.valid && uint32_t(millis() - s.updatedAt) <= 2000);
  flag(o, "networkEditable", canConfigureNetwork(s, controller_.pending(), pendingSettingsJob_ != 0));
  flag(o, "protocolMismatch", s.protocolMismatch); flag(o, "uncertain", s.uncertain); flag(o, "maintenance", s.maintenance); flag(o, "storageOk", storageOk_);
  num(o, "ageMs", s.valid ? uint32_t(millis() - s.updatedAt) : 0); num(o, "digits", s.settings.count); num(o, "selected", s.panel.selected);
  flag(o, "busy", s.panel.busy || controller_.pending()); flag(o, "saving", s.panel.saving || pendingSettingsJob_); flag(o, "watching", s.panel.watching); flag(o, "usbLocked", s.panel.usbLocked);
  str(o, "error", s.panel.error); cJSON_AddItemToObject(o, "controller", settingsJson(s.settings));
  cJSON* units = cJSON_AddArrayToObject(o, "units");
  for (uint8_t physical = 0; physical < 10; ++physical) {
    const auto& u = s.units[physical];
    cJSON* row = cJSON_CreateObject(); num(row, "unit", physical); flag(row, "enabled", u.enabled); flag(row, "armed", u.armed);
    num(row, "index", u.index); num(row, "target", u.target); num(row, "mode", u.mode);
    str(row, "reference", u.reference == 2 ? "HALL" : u.reference == 1 ? "MANUAL" : "NONE");
    const uint8_t remembered = displayMemory_.glyph(physical);
    if (remembered < 45) num(row, "rememberedIndex", remembered);
    else cJSON_AddNullToObject(row, "rememberedIndex");
    flag(row, "rawHigh", u.rawHigh); flag(row, "hall", u.hall); num(row, "steps", u.steps); num(row, "remaining", u.remaining); str(row, "error", u.error);
    cJSON* measures = cJSON_AddArrayToObject(row, "measured"); for (uint8_t i = 0; i < u.measuredCount; ++i) cJSON_AddItemToArray(measures, cJSON_CreateNumber(u.measured[i]));
    cJSON_AddItemToArray(units, row);
  }
  cJSON* jobs = cJSON_AddArrayToObject(o, "jobs");
  for (const auto& j : s.jobs) if (j.id) { cJSON* row = cJSON_CreateObject(); num(row, "id", j.id); num(row, "request", j.request); num(row, "op", j.op);
    const uint8_t stage = j.id == pendingSettingsJob_ && j.stage == uint8_t(sf::Stage::Done) ? uint8_t(sf::Stage::Accepted) : j.stage;
    str(row, "stage", stageName(stage)); str(row, "error", sf::errorName(j.error)); cJSON_AddItemToArray(jobs, row); }
  cJSON* display = cJSON_AddObjectToObject(o, "display"); str(display, "mode", settings_.mode); flag(display, "running", running_); str(display, "reason", pauseReason_);
  str(display, "clockFormat", settings_.clockFormat); str(display, "dateFormat", settings_.dateFormat); num(display, "align", uint8_t(settings_.align)); num(display, "timezone", settings_.timezone);
  num(display, "unix", double(time(nullptr))); str(display, "timeSource", timeSource_.c_str()); flag(display, "timeValid", time(nullptr) >= 1704067200LL && time(nullptr) < 4102444800LL);
  cJSON* net = cJSON_AddObjectToObject(o, "network"); str(net, "ssid", settings_.ssid); str(net, "apSsid", apSsid_.c_str()); str(net, "apIp", WiFi.softAPIP().toString().c_str());
  str(net, "staIp", WiFi.localIP().toString().c_str()); flag(net, "connected", WiFi.status() == WL_CONNECTED); num(net, "rssi", WiFi.RSSI());
  flag(o, "otaReady", otaReady(s)); str(o, "otaError", otaError_.c_str()); json(o);
}
void App::printAccess() {
  Serial.printf("\nAP SSID: %s\nAP password: %s\nURL: http://192.168.4.1/\nLogin: admin\nAdmin password: %s\n",
      apSsid_.c_str(), settings_.apPassword, settings_.adminPassword);
}
void App::resetNetwork() {
  AppSettings candidate = settings_; candidate.ssid[0] = candidate.wifiPassword[0] = 0;
  randomPassword(candidate.apPassword, 12); randomPassword(candidate.adminPassword, 12);
  if (!writeAppSettings(candidate)) { Serial.println("ERR NVS save failed; old credentials retained"); return; }
  settings_ = candidate; csrf_ = token(); connectNetwork(); printAccess();
}
void App::console() {
  for (uint8_t count = 0; count < 32 && Serial.available(); ++count) {
    const char c = char(Serial.read());
    if (c == '\r' || c == '\n') {
      if (!consoleDiscard_ && consoleLength_) {
        consoleLine_[consoleLength_] = 0;
        if (!strcmp(consoleLine_, "ACCESS")) printAccess();
        else if (!strcmp(consoleLine_, "STATUS")) {
          const State s = controller_.snapshot(); Serial.printf("Controller=MCP ready=%u busy=%u coils_off=%u digits=%u hall=%u mask=0x%02x required=0x%02x error=%s\n",
            s.connected, s.panel.busy || controller_.pending(), s.outputsDisabled, s.settings.count, s.hallEnabled, s.presentMask, s.requiredMask, s.panel.error);
          Serial.printf("AP=%s STA=%s running=%u maxTick=%luus maxWrite=%luus lateTicks=%lu\n",
            WiFi.softAPIP().toString().c_str(), WiFi.localIP().toString().c_str(), running_,
            static_cast<unsigned long>(s.maxTickGapUs), static_cast<unsigned long>(s.maxFlushUs), static_cast<unsigned long>(s.lateTicks));
          for (uint8_t i = 0; i < s.settings.count; ++i) {
            const auto& u = s.units[s.settings.mapping[i]];
            Serial.printf("U%u armed=%u reference=%u index=%u target=%u mode=%u steps=%lu remaining=%lu SPEED=%u ACCEL=%u REVSTEPS=%u error=%s\n",
              u.unit,u.armed,u.reference,u.index,u.target,u.mode,static_cast<unsigned long>(u.steps),
              static_cast<unsigned long>(u.remaining),u.config.speed,u.config.accel,u.config.revolution,u.error);
          }
          const Job* latest = nullptr;
          for (const auto& j : s.jobs) if (j.id && (!latest || int32_t(j.id - latest->id) > 0)) latest = &j;
          if (latest) Serial.printf("Last job=%lu stage=%s error=%s\n", static_cast<unsigned long>(latest->id),
            stageName(latest->stage), sf::errorName(latest->error));
        } else if (!strcmp(consoleLine_, "NETWORK RESET")) {
          pause("網路重設已暫停自動顯示。"); const uint8_t stop[] = {uint8_t(sf::Op::Stop), sf::kAll};
          resetStopJob_ = 0; controller_.submit(stop, sizeof(stop), 0, resetStopJob_); resetRequested_ = true; resetAt_ = millis();
          Serial.println("Network reset pending; stopping motors first. Calibration is retained.");
        } else if (!strcmp(consoleLine_, "HELP")) {
          Serial.println("ACCESS | STATUS | NETWORK RESET | INIT | STOP | UNIT 0..9 | ARM [ALL]");
          Serial.println("SET Z | SETTEXT ABC | SHOW Z | TEXT HELLO | STEP n | REV | FLAP | CYCLE [ALL]");
          Serial.println("DIGITS 1..10 | UNITS 0,2,7 | SPEED [ALL] 800 | ACCEL [ALL] 800 | SAVE | LOAD");
          Serial.println("REVSTEPS n | OFFSET n | DIR 1/-1 | ORDER 1234 | POLARITY LOW/HIGH");
          Serial.println("HOME [ALL] | MEASURE | WATCH (enable Hall in settings first); LED (unplug motor)");
        } else {
          const State s = controller_.snapshot(); uint8_t bytes[sf::kMaxPayload], n = 0; uint32_t id = 0;
          if (!consolePayload(consoleLine_, s.settings.count, s.panel.selected, bytes, n)) Serial.println("ERR command; HELP");
          else {
            const sf::Error result = controller_.submit(bytes, n, 0, id);
            if (result != sf::Error::None) Serial.printf("ERR %s\n", sf::errorName(result));
            else { pause("串列手動操作已暫停自動顯示。"); Serial.printf("ACCEPTED job=%lu; STATUS shows progress\n",static_cast<unsigned long>(id)); }
          }
        }
      }
      consoleLength_ = 0; consoleDiscard_ = false;
    } else if (uint8_t(c) < 32 || uint8_t(c) == 127 || consoleLength_ == sizeof(consoleLine_) - 1) { consoleLength_ = 0; consoleDiscard_ = true; }
    else if (!consoleDiscard_) consoleLine_[consoleLength_++] = char(toupper(uint8_t(c)));
  }
}
}
