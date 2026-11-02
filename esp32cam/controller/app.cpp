// SPDX-License-Identifier: GPL-3.0-only
#include "app.h"
#include "web_assets.h"
#include <esp_app_format.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <esp_system.h>

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
  Serial.begin(115200);
  // NodeMCU ESP-32S has no camera. Leave unrelated GPIOs untouched.
  // Enable the RF entropy source before generating per-device passwords and session nonces.
  WiFi.persistent(false); WiFi.setHostname("splitflap"); WiFi.mode(WIFI_AP_STA); WiFi.setAutoReconnect(true);
  storageOk_ = store_.begin(settings_);
  if (storageOk_) store_.loadDisplayMemory(displayMemory_);
  uint8_t sessionData[SessionTable::kSize];
  if (storageOk_ && store_.readSessions(sessionData, sizeof(sessionData))) sessions_.decode(sessionData, sizeof(sessionData));
  bootAt_ = millis(); recoveryPending_ = storageOk_ && settings_.autoRecovery && esp_reset_reason() == ESP_RST_POWERON;
  playItem_ = settings_.itemIndex;
  if (!validAppSettings(settings_)) { randomPassword(settings_.apPassword, 12); randomPassword(settings_.adminPassword, 12); }
  csrf_ = token(); apSsid_ = "SplitFlap-" + String(uint32_t(ESP.getEfuseMac() >> 24) & 0xffffff, HEX);
  bridge_.begin();
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid_.c_str(), settings_.apPassword, 1, 0, 4);
  dns_.start(53, "*", WiFi.softAPIP());
  if (settings_.ssid[0]) WiFi.begin(settings_.ssid, settings_.wifiPassword);
  MDNS.begin("splitflap"); MDNS.addService("http", "tcp", 80);
  sntp_set_time_sync_notification_cb(ntpCallback); configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  const char* headers[] = {"X-CSRF-Token", "Origin", "X-OTA-Ticket", "Cookie", "Content-Type"}; server_.collectHeaders(headers, 5);
  routes(); server_.begin(); mbedtls_md_init(&hash_);
#ifdef SPLITFLAP_SINGLE
  Serial.printf("Split-Flap %s %s. MCP23017 I2C SDA=%u SCL=%u RESET=%u, 400kHz.\n",
                kHardwareName, kFirmwareVersion, kMcpSda, kMcpScl, kMcpReset);
#else
  Serial.printf("Split-Flap %s %s. UART2 RX=%u TX=%u; Mega Serial1 115200.\n",
                kHardwareName, kFirmwareVersion, kUartRx, kUartTx);
#endif
  Serial.println("ACCESS: show device Wi-Fi and admin credentials. STATUS | NETWORK RESET | HELP");
  if (store_.fresh() || !storageOk_) printAccess();
  if (!storageOk_) Serial.println("FAULT: NVS unavailable; settings are not durable.");
}
void App::pause(const char* reason) {
  running_ = false;
  if (automaticJob_) captureJob_ = automaticJob_;
  automaticJob_ = automaticAckAt_ = 0;
  strncpy(pauseReason_, reason, sizeof(pauseReason_) - 1); pauseReason_[sizeof(pauseReason_) - 1] = 0;
}
void App::tick() {
  rememberDisplay();
  server_.handleClient(); dns_.processNextRequest(); console();
  if (ntpReceived) { ntpReceived = false; timeSource_ = "ntp"; }
  if (networkAt_ && int32_t(millis() - networkAt_) >= 0) { networkAt_ = 0; connectNetwork(); }
  if (restartAt_ && int32_t(millis() - restartAt_) >= 0) ESP.restart();
  if (pendingSettingsJob_) {
    const State s = bridge_.snapshot();
    for (const auto& job : s.jobs) if (job.id == pendingSettingsJob_ && job.stage >= uint8_t(sf::Stage::Done)) {
      if (job.stage == uint8_t(sf::Stage::Done)) {
        if (store_.save(pendingSettings_)) { settings_ = pendingSettings_; storageOk_ = true; }
        else {
          storageOk_ = false; bridge_.configurationFailed(pendingSettingsJob_);
          pause(SF_PEER " 已保存；ESP32 顯示偏好保存失敗，請重新匯入。");
        }
      } else pause(SF_PEER " 拒絕匯入；ESP32 顯示偏好未更改。");
      pendingSettingsJob_ = 0; break;
    }
  }
  if (resetRequested_) {
    const State s = bridge_.snapshot(); bool stopped = false;
    for (const auto& j : s.jobs) if (j.id == resetStopJob_ && j.stage == uint8_t(sf::Stage::Done)) stopped = true;
    if (stopped || (!s.connected && uint32_t(millis() - resetAt_) > 2500)) { resetRequested_ = false; resetNetwork(); }
  }
  flowTick(); recoveryTick(); scheduler(); delay(1);
}
void App::rememberDisplay() {
  const State s = bridge_.snapshot(); const uint32_t now = millis();
  if (!s.connected || !s.valid || uint32_t(now - s.updatedAt) > 2000 ||
      s.panel.busy || s.panel.saving || s.panel.watching || s.maintenance || bridge_.pending()) return;
  if (captureJob_) {
    for (const auto& j : s.jobs) if (j.id == captureJob_ && j.stage >= uint8_t(sf::Stage::Done)) {
      if (j.stage == uint8_t(sf::Stage::Done)) {
        if (!automaticAckAt_) { automaticAckAt_ = now; return; }
        if (int32_t(s.updatedAt - automaticAckAt_) < 0) return;
        captureDisplay(true);
      }
      automaticAckAt_ = 0;
      captureJob_ = 0;
    }
  }
  if (!displayMemory_.dirty() || flow_.active() || calibrating_) return;
  if (captureDynamic_ && lastMemorySave_ && uint32_t(now - lastMemorySave_) < 60000) return;
  if (displayMemoryRetryAt_ && int32_t(now - displayMemoryRetryAt_) < 0) return;
  displayMemoryOk_ = store_.saveDisplayMemory(displayMemory_);
  if (displayMemoryOk_) { displayMemory_.saved(); displayMemoryRetryAt_ = 0; lastMemorySave_ = now; }
  else displayMemoryRetryAt_ = now + 5000;
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
  if (flow_.active() || pendingSettingsJob_ || s.maintenance || s.panel.busy || s.panel.saving || s.panel.watching || bridge_.pending()) {
    error(409, "請先停止或等待目前工作完成。"); return false;
  }
  return true;
}
bool App::saveApp(const AppSettings& candidate) {
  if (!store_.save(candidate)) { error(500, "設定保存失敗；原設定保留。"); return false; }
  settings_ = candidate; storageOk_ = true; return true;
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
  server_.on("/wiring-mega.svg", HTTP_GET, [wiring]() { wiring(kWiringMegaGzip, sizeof(kWiringMegaGzip)); });
  server_.on("/wiring-hall.svg", HTTP_GET, [wiring]() { wiring(kWiringHallGzip, sizeof(kWiringHallGzip)); });
  server_.on("/", HTTP_GET, [this]() {
    server_.sendHeader("Content-Encoding", "gzip"); server_.sendHeader("Cache-Control", "no-cache");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.sendHeader("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; form-action 'self'");
    server_.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(kWebGzip), sizeof(kWebGzip));
  });
  server_.on("/api/login", HTTP_POST, [this]() { login(); });
  server_.on("/api/logout", HTTP_POST, [this]() { logout(); });
  server_.on("/api/session", HTTP_GET, [this]() { session(); });
  server_.on("/api/calibration", HTTP_POST, [this]() { if (control()) calibration(); });
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
  const State s = bridge_.snapshot(); uint32_t request = 0; uint8_t bytes[sf::kMaxPayload], n = 0;
  if (!requestId(b, s, request)) { cJSON_Delete(b); return; }
  Align align = settings_.align; int64_t alignment;
  const cJSON* field = cJSON_GetObjectItemCaseSensitive(b, "align");
  if (field) { if (!integer(field, 0, 2, alignment)) { cJSON_Delete(b); error(400, "對齊方式無效。"); return; } align = Align(alignment); }
  const bool ok = commandPayload(b, s.settings.count, align, bytes, n); cJSON_Delete(b);
  if (!ok) { error(400, "命令、字元或參數無效。"); return; }
  const sf::Op op = sf::Op(bytes[0]); const uint8_t unit = bytes[1];
  if (op == sf::Op::Units || op == sf::Op::Digits || op == sf::Op::Arm) { error(400, "安裝範圍請使用位數設定；動作會自動準備。"); return; }
  if (op == sf::Op::Stop) {
    abortFlow(); pause("已停止自動顯示。"); persistPaused();
    uint32_t id = 0; const auto result = bridge_.submit(bytes, n, request, id);
    if (result != sf::Error::None) { error(409, sf::errorName(result)); return; }
    cJSON* o = cJSON_CreateObject(); num(o, "id", id); json(o, 202); return;
  }
  if (op == sf::Op::Claim) {
    if (calibrating_ || !idle(s)) { if (calibrating_) error(409, "請先取消校準。"); return; }
    uint32_t id = 0; const auto result = bridge_.submit(bytes, n, request, id);
    if (result != sf::Error::None) { error(409, sf::errorName(result)); return; }
    pause("已接管網頁控制。"); persistPaused();
    cJSON* o = cJSON_CreateObject(); num(o, "id", id); json(o, 202); return;
  }
  if (request < s.nextRequest) { beginFlow(Purpose::Action, bytes, n, request); return; }
  if (calibrating_ || recoveryPending_) { error(409, "請先完成或取消歸零流程。"); return; }
  if (unit != sf::kAll && unit >= s.settings.count) { error(400, "位置超出已設定的位數。"); return; }
  const bool needsPosition = op == sf::Op::Text || op == sf::Op::Goto || op == sf::Op::Flap || op == sf::Op::Cycle;
  const bool movement = needsPosition || op == sf::Op::Home || op == sf::Op::Step || op == sf::Op::Rev || op == sf::Op::Measure || op == sf::Op::Led;
  bool needHome = false;
  for (uint8_t i = 0; i < s.settings.count; ++i) if ((unit == sf::kAll || unit == i) && !s.units[i].reference) needHome = true;
  if (needsPosition && needHome && !calibrated(settings_, s.settings, unit)) { error(409, "請先完成歸零校準，或記錄目前實際字元。"); return; }
  if (!beginFlow(Purpose::Action, bytes, n, request)) return;
  pause("已切換為手動操作。"); persistPaused();
  if (movement) flow_.add(sf::Op::Arm, unit);
  if (needsPosition && needHome) flow_.add(sf::Op::Home, unit);
  flow_.add(bytes, n);
  flowRemember_ = op == sf::Op::Text || op == sf::Op::Goto || op == sf::Op::SetIndex || op == sf::Op::SetText || op == sf::Op::Flap;
}
void App::configuration() {
  if (calibrating_ || recoveryPending_) { error(409, "請先完成或取消歸零校準。"); return; }
  cJSON* b = body(); if (!b) return;
  const State s = bridge_.snapshot(); uint32_t request = 0; sf::Settings config;
  if (!requestId(b, s, request)) { cJSON_Delete(b); return; }
  int64_t schema = 0;
  const char* device = stringField(b, "device");
  const bool ok = (!device || !strcmp(device, "splitflap-esp32cam") || !strcmp(device, "splitflap-esp32s")) && fieldInteger(b, "schema", 1, 2, schema) &&
      parseSettings(cJSON_GetObjectItemCaseSensitive(b, "mega"), config) && contiguous(config);
  AppSettings candidate = settings_;
  const cJSON* prefs = cJSON_GetObjectItemCaseSensitive(b, "display");
  bool prefsOk = true;
  if (prefs) {
    prefsOk = parseProgram(prefs, candidate);
  }
  cJSON_Delete(b);
  if (!ok || !prefsOk) { error(400, "設定無效；只接受 U0 起的連續位置，未套用。"); return; }
  candidate.resumeDisplay = false;
  for (uint8_t i = 0; i < 10; ++i) if (i >= s.settings.count || i >= config.count || candidate.signatures[i] != geometrySignature(config.units[i])) candidate.calibratedMask &= ~(1U << i);
  if (!idle(s)) return;
  pause("設定已保存；速度調整保留目前字元與轉動許可。定位校準有變更時，請確認實際字元。");
  uint8_t bytes[sf::kSettingsSize + 2]; bytes[0] = uint8_t(sf::Op::ApplyConfig); bytes[1] = sf::kAll;
  sf::encodeSettings(config, bytes + 2); uint32_t id = 0;
  const sf::Error result = bridge_.submit(bytes, sizeof(bytes), request, id);
  if (result != sf::Error::None) { error(409, sf::errorName(result)); return; }
  // Display preferences never contain movement or arming state.
  if (request >= s.nextRequest) { pendingSettings_ = candidate; pendingSettingsJob_ = id; }
  cJSON* response = cJSON_CreateObject(); num(response, "id", id); json(response, 202);
}
void App::getConfiguration() {
  if (pendingSettingsJob_) { error(409, "設定正在保存，請完成後再匯出。"); return; }
  const State s = bridge_.snapshot(); if (!s.valid) { error(503, "尚未讀到 " SF_PEER " 設定。"); return; }
  cJSON* o = cJSON_CreateObject(); num(o, "schema", 2); str(o, "device", "splitflap-esp32s");
  cJSON* mega = settingsJson(s.settings); cJSON_DeleteItemFromObjectCaseSensitive(mega, "units"); cJSON_AddItemToObject(o, "mega", mega);
  cJSON_AddItemToObject(o, "display", programJson(settings_)); json(o);
}
void App::network() {
  const State s = bridge_.snapshot();
  if (flow_.active() || calibrating_ || recoveryPending_ || !canConfigureNetwork(s, bridge_.pending(), pendingSettingsJob_ != 0)) {
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
  const bool passwordChanged = strcmp(candidate.adminPassword, settings_.adminPassword);
  if (passwordChanged) { SessionTable empty; if (!saveSessions(empty)) { error(500, "登入記錄無法更新，密碼尚未修改。"); return; } }
  candidate.resumeDisplay = false;
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
  const cJSON* run = cJSON_GetObjectItemCaseSensitive(b, "run");
  if (run && !cJSON_IsBool(run)) { cJSON_Delete(b); error(400, "run 必須是布林值。"); return; }
  const bool start = cJSON_IsTrue(run);
  if (!start && !cJSON_GetObjectItemCaseSensitive(b, "mode") && run) {
    cJSON_Delete(b); pause("自動顯示已暫停。"); persistPaused(); json(cJSON_CreateObject()); return;
  }
  const State s = bridge_.snapshot(); uint32_t request = 0;
  const String raw = server_.arg("plain"); const uint16_t fingerprint = sf::crc16(reinterpret_cast<const uint8_t*>(raw.c_str()), raw.length());
  const uint8_t key[] = {242, uint8_t(fingerprint), uint8_t(fingerprint >> 8)};
  if (start) {
    if (!requestId(b, s, request)) { cJSON_Delete(b); return; }
    if (request < s.nextRequest) { cJSON_Delete(b); beginFlow(Purpose::StartDisplay, key, sizeof(key), request); return; }
    if (!s.connected || !s.valid || !s.hardwareReady || s.panel.usbLocked || !contiguous(s.settings) || uint32_t(millis() - s.updatedAt) > 2000) {
      cJSON_Delete(b); error(409, SF_PEER " 尚未就緒。"); return;
    }
  }
  if (calibrating_ || recoveryPending_ || !idle(s)) { cJSON_Delete(b); if (calibrating_ || recoveryPending_) error(409, "請先完成歸零流程。"); return; }
  AppSettings candidate = settings_; const bool changesMode = cJSON_GetObjectItemCaseSensitive(b, "mode") != nullptr;
  bool ok = parseProgram(b, candidate);
  if (cJSON_GetObjectItemCaseSensitive(b, "autoRecovery")) {
    candidate.recoveryChosen = true;
    if (candidate.autoRecovery && !calibrated(candidate, s.settings)) ok = false;
  }
  cJSON_Delete(b);
  if (!ok || (start && !programFits(candidate, s.settings.count))) { error(400, "顯示格式、位數或復電校準尚未就緒。"); return; }
  bool needHome = false;
  for (uint8_t i = 0; i < s.settings.count; ++i) if (!s.units[i].reference) needHome = true;
  if (start && needHome && !calibrated(candidate, s.settings)) { error(409, "請先完成歸零校準或記錄目前字元。"); return; }
  // Saving a new program is deliberately distinct from starting it.
  if (start || changesMode) candidate.resumeDisplay = start;
  if (!saveApp(candidate)) return;
  playItem_ = candidate.itemIndex; itemSince_ = 0;
  if (!start) { if (changesMode) pause("顯示設定已保存，按開始後執行。"); json(cJSON_CreateObject()); return; }
  if (!beginFlow(Purpose::StartDisplay, key, sizeof(key), request)) return;
  pause("正在準備自動顯示。"); flow_.add(sf::Op::Arm);
  if (needHome) flow_.add(sf::Op::Home);
}
void App::setTime() {
  cJSON* b = body(); if (!b) return; int64_t seconds;
  const bool ok = fieldInteger(b, "unix", 1704067200.0, 4102444799.0, seconds); cJSON_Delete(b);
  if (!ok) { error(400, "時間必須介於 2024～2099 年。"); return; }
  timeval tv{}; tv.tv_sec = time_t(seconds); settimeofday(&tv, nullptr); timeSource_ = "phone"; json(cJSON_CreateObject());
}
bool App::displayTarget(uint8_t* out) {
  const uint8_t width = bridge_.snapshot().settings.count;
  const char* mode = settings_.mode;
  const char* format = !strcmp(mode, "clock") ? settings_.clockFormat : !strcmp(mode, "date") ? settings_.dateFormat : settings_.weekdayFormat;
  Align align = settings_.align;
  if (!strcmp(mode, "carousel")) {
    const DisplayItem& item = settings_.items[playItem_]; mode = item.mode; format = item.format; align = item.align;
    if (!strcmp(mode, "text")) return textIndices(item.text, width, align, false, out);
  }
  return formatTime(time(nullptr), settings_.timezone, mode, format, width, align, out, settings_.leadingZero);
}
void App::scheduler() {
  if (!running_ || flow_.active() || calibrating_ || recoveryPending_ || uint32_t(millis() - scheduleAt_) < 200) return;
  scheduleAt_ = millis(); const State s = bridge_.snapshot();
  if (!s.connected || !s.valid || !s.hardwareReady || s.uncertain || s.panel.usbLocked || s.panel.usbRevision != lastUsbRevision_) {
    pause(kSingleController ? "MCP23017 故障或重新初始化，請重新開始顯示。" : "連線或控制權變更，請重新開始顯示。"); return;
  }
  if (automaticJob_) {
    const Job* job = nullptr; for (const auto& j : s.jobs) if (j.id == automaticJob_) job = &j;
    if (!job || job->stage >= uint8_t(sf::Stage::Rejected)) { pause("顯示工作失敗，請檢查狀態。"); return; }
    if (job->stage != uint8_t(sf::Stage::Done)) return;
    if (!automaticAckAt_) { automaticAckAt_ = millis(); return; }
    if (int32_t(s.updatedAt - automaticAckAt_) < 0 || s.panel.busy || bridge_.pending()) return;
    captureDisplay(true); automaticJob_ = automaticAckAt_ = 0;
    // Seconds mode may submit a newer target immediately; flush a due checkpoint
    // in this idle gap instead of waiting for an idle tick that may never occur.
    rememberDisplay();
    if (!itemSince_) {
      itemSince_ = millis();
      if (settings_.itemIndex != playItem_) { settings_.itemIndex = playItem_; if (!store_.save(settings_)) storageOk_ = false; }
    }
  }
  if (s.panel.busy || s.panel.saving || bridge_.pending()) return;
  if (!programWindowOpen(settings_, time(nullptr))) {
    itemSince_ = 0; copyString("顯示時段外或等待校時，保留最後畫面。", pauseReason_, sizeof(pauseReason_)); return;
  }
  if (!strcmp(settings_.mode, "carousel") && itemSince_ && uint32_t(millis() - itemSince_) >= uint32_t(settings_.items[playItem_].seconds) * 1000) {
    playItem_ = (playItem_ + 1) % settings_.itemCount; itemSince_ = 0;
  }
  uint8_t bytes[13] = {uint8_t(sf::Op::Text), sf::kAll, s.settings.count};
  if (!displayTarget(bytes + 3)) { copyString("等待校時，或目前格式放不下；已保留最後畫面。", pauseReason_, sizeof(pauseReason_)); return; }
  bool changed = false;
  for (uint8_t i = 0; i < s.settings.count; ++i) {
    if (!s.units[i].armed || !s.units[i].reference) { pause("定位已失效，請重新開始顯示。"); return; }
    if (s.units[i].index != bytes[3 + i]) changed = true;
  }
  copyString("自動顯示執行中；忙碌時略過過期時間。", pauseReason_, sizeof(pauseReason_));
  if (!changed) { if (!itemSince_) { itemSince_ = millis(); if (settings_.itemIndex != playItem_) { settings_.itemIndex = playItem_; if (!store_.save(settings_)) storageOk_ = false; } } return; }
  const auto result = bridge_.submit(bytes, 3 + s.settings.count, 0, automaticJob_);
  if (result != sf::Error::None && result != sf::Error::Busy) pause("無法送出顯示工作。");
}
bool App::otaReady(const State& s) const {
  return otaStopped(s, otaStopJob_) && uint32_t(millis() - s.updatedAt) <= 1500;
}
bool App::awaitOtaState() {
  for (;;) {
    const State s = bridge_.snapshot();
    const auto result = otaGuard_.check(s, millis());
    if (result == OtaGuard::Result::Ready) return true;
    if (result == OtaGuard::Result::Waiting) { delay(2); continue; }
    const char* reason = "更新期間 " SF_PEER " 停止狀態或控制設定已改變。";
    if (result == OtaGuard::Result::LinkLost) reason = "更新期間 " SF_PEER " 通訊已中斷或重新連線。";
    else if (result == OtaGuard::Result::MaintenanceLost) reason = "更新維護鎖已解除，請重新準備更新。";
    else if (result == OtaGuard::Result::Timeout) reason = "更新寫入後未能取得 " SF_PEER " 新狀態，已中止並保留原韌體。";
    otaFailure(reason); return false;
  }
}
void App::prepareOta() {
  if (calibrating_) { error(409, "請先完成或取消校準。"); return; }
  cJSON* b = body(); if (!b) return; const State s = bridge_.snapshot(); uint32_t request = 0;
  if (!requestId(b, s, request)) { cJSON_Delete(b); return; } cJSON_Delete(b);
  abortFlow(); pause("已準備更新 ESP32，更新後不自動轉動。");
  const uint8_t stop[] = {uint8_t(sf::Op::Stop), sf::kAll};
  const sf::Error result = bridge_.submit(stop, sizeof(stop), request, otaStopJob_);
  if (result != sf::Error::None) { error(409, sf::errorName(result)); return; }
  otaTicket_ = token(); otaError_ = ""; otaFileSeen_ = false; otaSucceeded_ = false;
  cJSON* o = cJSON_CreateObject(); num(o, "id", otaStopJob_); str(o, "ticket", otaTicket_.c_str()); json(o, 202);
}
void App::otaFailure(const char* reason) {
  otaError_ = reason; otaAuthorized_ = false; otaSucceeded_ = false;
  if (otaStarted_) { Update.abort(); otaStarted_ = false; }
  mbedtls_md_free(&hash_); mbedtls_md_init(&hash_); hashActive_ = false;
  bridge_.maintenance(false); otaTicket_ = "";
}
void App::uploadOta() {
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (otaFileSeen_) { otaFailure("一次只能上傳一個韌體檔案，請重新準備更新。"); return; }
    otaFileSeen_ = true;
    otaSucceeded_ = false; otaError_ = ""; otaHeaderAt_ = imagePrefixAt_ = 0; otaSize_ = otaReceived_ = 0;
    const State s = bridge_.snapshot();
    if (!authenticated() || !csrfMatches() || !otaTicket_.length() ||
        !same(server_.header("X-OTA-Ticket"), otaTicket_) || !otaReady(s) || !bridge_.maintenance(true)) {
      otaFailure("更新未授權，或尚未取得 " SF_PEER " 停止確認。"); return;
    }
    otaAuthorized_ = true;
    otaGuard_.begin(s, otaStopJob_);
    if (mbedtls_md_setup(&hash_, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) != 0 || mbedtls_md_starts(&hash_) != 0) {
      otaFailure("無法初始化映像校驗。"); return;
    }
    hashActive_ = true;
  } else if (upload.status == UPLOAD_FILE_WRITE && otaAuthorized_) {
    if (!awaitOtaState()) return;
    const size_t written = otaStarted_ ? Update.progress() : 0;
    otaChunk(upload.buf, upload.currentSize);
    if (otaAuthorized_ && otaStarted_ && Update.progress() != written) {
      const State s = bridge_.snapshot();
      otaGuard_.afterWrite(s, millis());
      awaitOtaState();
    }
  } else if (upload.status == UPLOAD_FILE_END && otaAuthorized_) {
    if (!awaitOtaState()) return;
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
  if (!awaitOtaState()) { error(409, otaError_.c_str()); return; }
  if (!Update.end(true)) { otaFailure("ESP32 拒絕此映像，保留目前韌體。"); error(400, otaError_.c_str()); return; }
  otaStarted_ = false; otaTicket_ = "";
  cJSON* o = cJSON_CreateObject(); str(o, "message", "更新完成，即將重新啟動；不會自動轉動馬達。"); json(o);
  restartAt_ = millis() + 1200;
}
void App::state() {
  const State s = bridge_.snapshot(); cJSON* o = cJSON_CreateObject();
  str(o, "firmware", kFirmwareVersion); str(o, "hardware", kHardwareName);
  num(o, "protocol", sf::kVersion); num(o, "boot", s.boot); num(o, "nextRequest", s.nextRequest); str(o, "csrf", csrf_.c_str());
  flag(o, "displayMemoryOk", displayMemoryOk_);
  flag(o, "connected", s.connected && s.valid && uint32_t(millis() - s.updatedAt) <= 2000);
  flag(o, "networkEditable", !flow_.active() && !calibrating_ && !recoveryPending_ && canConfigureNetwork(s, bridge_.pending(), pendingSettingsJob_ != 0));
  flag(o, "protocolMismatch", s.protocolMismatch); flag(o, "uncertain", s.uncertain); flag(o, "maintenance", s.maintenance); flag(o, "storageOk", storageOk_);
  num(o, "ageMs", s.valid ? uint32_t(millis() - s.updatedAt) : 0); num(o, "digits", s.settings.count); num(o, "selected", s.panel.selected);
  flag(o, "busy", s.panel.busy || bridge_.pending() || flow_.active() || recoveryPending_); flag(o, "saving", s.panel.saving || pendingSettingsJob_); flag(o, "watching", s.panel.watching); flag(o, "usbLocked", s.panel.usbLocked);
  cJSON* workflow = cJSON_AddObjectToObject(o, "workflow"); flag(workflow, "active", flow_.active()); flag(workflow, "calibrating", calibrating_);
  flag(workflow, "calibrated", calibrated(settings_, s.settings)); flag(workflow, "recovering", recoveryPending_ || (flow_.active() && flow_.purpose() == Purpose::Recovery));
  num(workflow, "step", flow_.step()); num(workflow, "steps", flow_.count()); str(workflow, "message", flowMessage_);
  num(workflow, "jogReadyMask", calJogReady_);
  str(o, "error", s.panel.error); cJSON_AddItemToObject(o, "mega", settingsJson(s.settings));
  flag(o, "single", kSingleController);
  if (kSingleController) {
    cJSON* mcp = cJSON_AddObjectToObject(o, "mcp"); flag(mcp, "ready", s.hardwareReady); flag(mcp, "outputsOff", s.outputsDisabled);
    num(mcp, "present", s.presentMask); num(mcp, "required", s.requiredMask); flag(mcp, "hall", s.hallEnabled);
    flag(mcp, "configStorageOk", s.configStorageOk); num(mcp, "maxTickGapUs", s.maxTickGapUs);
    num(mcp, "lateTicks", s.lateTicks); num(mcp, "maxFlushUs", s.maxFlushUs);
  }
  cJSON* units = cJSON_AddArrayToObject(o, "units");
  for (uint8_t physical = 0; physical < 10; ++physical) {
    const auto& u = s.units[physical];
    cJSON* row = cJSON_CreateObject(); num(row, "unit", physical); flag(row, "enabled", u.enabled); flag(row, "armed", u.armed);
    num(row, "index", u.index); num(row, "target", u.target); num(row, "mode", u.mode);
    flag(row, "calibrated", calibrated(settings_, s.settings, physical));
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
  cJSON* display = programJson(settings_); cJSON_AddItemToObject(o, "display", display); flag(display, "running", running_); str(display, "reason", pauseReason_);
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
  SessionTable empty;
  if (!saveSessions(empty) || !store_.save(candidate)) { Serial.println("ERR NVS save failed; old credentials retained"); return; }
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
          const State s = bridge_.snapshot();
          if (kSingleController) Serial.printf("MCP=%s present=0x%x required=0x%x error=%s late=%lu maxGap=%luus maxFlush=%luus\n",
            s.hardwareReady ? "ready" : "offline", s.presentMask, s.requiredMask, s.panel.error[0] ? s.panel.error : "none",
            static_cast<unsigned long>(s.lateTicks), static_cast<unsigned long>(s.maxTickGapUs), static_cast<unsigned long>(s.maxFlushUs));
          else Serial.printf("Mega=%s ", s.connected ? "connected" : "offline");
          Serial.printf("digits=%u AP=%s STA=%s running=%u\n",
            s.settings.count, WiFi.softAPIP().toString().c_str(), WiFi.localIP().toString().c_str(), running_);
        } else if (!strcmp(consoleLine_, "NETWORK RESET")) {
          pause("網路重設已暫停自動顯示。"); const uint8_t stop[] = {uint8_t(sf::Op::Stop), sf::kAll};
          resetStopJob_ = 0; bridge_.submit(stop, sizeof(stop), 0, resetStopJob_); resetRequested_ = true; resetAt_ = millis();
          Serial.println("Network reset pending; stopping motors first. Calibration is retained.");
        } else Serial.println("ACCESS | STATUS | NETWORK RESET | HELP");
      }
      consoleLength_ = 0; consoleDiscard_ = false;
    } else if (c < 32 || c > 126 || consoleLength_ == sizeof(consoleLine_) - 1) { consoleLength_ = 0; consoleDiscard_ = true; }
    else if (!consoleDiscard_) consoleLine_[consoleLength_++] = char(toupper(uint8_t(c)));
  }
}
}
