// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <mbedtls/md.h>
#include "bridge.h"
#include "app_settings.h"
#include "json_helpers.h"
#include "program_json.h"
#include "session_table.h"
#include "action_flow.h"
#include "ota_guard.h"

namespace cam {
class App {
 public:
  void begin();
  void tick();
 private:
  void routes();
  bool auth();
  bool authenticated(uint8_t* digest = nullptr);
  bool saveSessions(const SessionTable& candidate);
  void login();
  void logout();
  void session();
  bool csrfMatches() const;
  bool control();
  cJSON* body();
  void json(cJSON* o, int code = 200);
  void error(int code, const char* message);
  bool requestId(const cJSON* body, const State& state, uint32_t& request);
  void command();
  void configuration();
  void network();
  void preferences();
  void setTime();
  void prepareOta();
  void uploadOta();
  void otaChunk(uint8_t* data, size_t size);
  void otaFailure(const char* reason);
  void finishOta();
  bool otaReady(const State& state) const;
  bool awaitOtaState();
  void state();
  void rememberDisplay();
  void getConfiguration();
  bool idle(const State& state);
  void pause(const char* reason);
  void scheduler();
  void calibration();
  bool beginFlow(Purpose purpose, const uint8_t* key, size_t size, uint32_t request);
  void flowTick();
  void recoveryTick();
  void abortFlow();
  void captureDisplay(bool dynamic = false);
  bool displayTarget(uint8_t* out);
  void persistPaused();
  void connectNetwork();
  void printAccess();
  void console();
  void resetNetwork();
  bool saveApp(const AppSettings& candidate);
  Bridge bridge_;
  AppSettings pendingSettings_;
  WebServer server_{80}; DNSServer dns_; AppStore store_; AppSettings settings_;
  SessionTable sessions_;
  ActionFlow flow_;
  OtaGuard otaGuard_;
  sf::Settings calibrationOriginal_;
  bool calibrating_ = false, recoveryPending_ = false, captureDynamic_ = false;
  bool flowRemember_ = false, flowDynamic_ = false;
  bool recoveryDisplayQueued_ = false;
  uint8_t loginFailures_ = 0;
  uint8_t playItem_ = 0;
  uint16_t calJogReady_ = 0;
  uint32_t loginRetryAt_ = 0, bootAt_ = 0, lastMemorySave_ = 0, itemSince_ = 0;
  uint32_t captureJob_ = 0, automaticAckAt_ = 0;
  char flowMessage_[128] = "待命";
  DisplayMemory displayMemory_;
  bool displayMemoryOk_ = true;
  uint32_t displayMemoryRetryAt_ = 0;
  String apSsid_, csrf_, otaTicket_, otaError_;
  char consoleLine_[64] = {}, pauseReason_[96] = "請完成歸零校準，或記錄目前字元。";
  uint8_t consoleLength_ = 0;
  bool consoleDiscard_ = false, running_ = false, storageOk_ = false;
  bool otaAuthorized_ = false, otaStarted_ = false, otaSucceeded_ = false, hashActive_ = false;
  bool otaFileSeen_ = false;
  bool resetRequested_ = false;
  uint32_t networkAt_ = 0, restartAt_ = 0, scheduleAt_ = 0, lastUsbRevision_ = 0;
  uint32_t automaticJob_ = 0, otaStopJob_ = 0, resetStopJob_ = 0, resetAt_ = 0;
  uint32_t pendingSettingsJob_ = 0;
  uint8_t otaHeader_[64] = {}, imagePrefix_[32] = {};
  size_t otaHeaderAt_ = 0, imagePrefixAt_ = 0;
  uint32_t otaSize_ = 0, otaReceived_ = 0;
  mbedtls_md_context_t hash_;
  String timeSource_ = "none";
};
}
