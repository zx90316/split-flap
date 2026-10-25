// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <mbedtls/md.h>
#include "local_controller.h"
#include "app_settings.h"
#include "json_helpers.h"

namespace cam {
class App {
 public:
  void begin();
  void tick();
 private:
  void routes();
  bool auth();
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
  void state();
  void rememberDisplay();
  void getConfiguration();
  bool idle(const State& state);
  void pause(const char* reason);
  void scheduler();
  void connectNetwork();
  void printAccess();
  void console();
  void resetNetwork();
  bool saveApp(const AppSettings& candidate);
  bool writeAppSettings(const AppSettings& candidate);
  LocalController controller_;
  AppSettings pendingSettings_;
  WebServer server_{80}; DNSServer dns_; AppStore store_; AppSettings settings_;
  DisplayMemory displayMemory_;
  bool displayMemoryOk_ = true;
  uint32_t displayMemoryRetryAt_ = 0;
  String apSsid_, csrf_, otaTicket_, otaError_;
  char consoleLine_[64] = {}, pauseReason_[96] = "請先定位、啟用馬達，再開始顯示。";
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
