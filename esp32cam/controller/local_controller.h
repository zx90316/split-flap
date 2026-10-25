// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include "mcp_bank.h"
#include "motion_store.h"
#include "local_engine.h"

namespace cam {
struct McpWire {
  static const uint8_t kSda = 13, kScl = 14, kReset = 15;
  static void bootReset() {
    // ESP32 core 3.x ignores digitalWrite before GPIO ownership is established.
    gpio_set_level(static_cast<gpio_num_t>(kReset), 0);
    pinMode(kReset, OUTPUT_OPEN_DRAIN); digitalWrite(kReset, LOW);
  }
  uint32_t nowUs() const { return micros(); }
  uint32_t nowMs() const { return millis(); }
  void delayUs(uint32_t n) { delayMicroseconds(n); }
  void reset(bool hold) { digitalWrite(kReset, hold ? LOW : HIGH); }
  bool probe(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }
  bool write16(uint8_t a, uint8_t r, uint16_t v) {
    Wire.beginTransmission(a); Wire.write(r); Wire.write(uint8_t(v)); Wire.write(uint8_t(v >> 8));
    return Wire.endTransmission() == 0;
  }
  bool read8(uint8_t a, uint8_t r, uint8_t& v) {
    Wire.beginTransmission(a); Wire.write(r);
    if (Wire.endTransmission(false) || Wire.requestFrom(a, size_t(1), true) != 1) return false;
    v = Wire.read(); return true;
  }
  bool read16(uint8_t a, uint8_t r, uint16_t& v) {
    Wire.beginTransmission(a); Wire.write(r);
    if (Wire.endTransmission(false) || Wire.requestFrom(a, size_t(2), true) != 2) return false;
    v = uint16_t(Wire.read()); v |= uint16_t(Wire.read()) << 8; return true;
  }
};
class MotionNvs {
 public:
  void begin() { ok_ = prefs_.begin("flap-motion", false); }
  bool healthy() const { return ok_; }
  bool read(uint8_t slot, uint8_t* bytes, size_t n) {
    return ok_ && prefs_.getBytesLength(key(slot)) == n && prefs_.getBytes(key(slot), bytes, n) == n;
  }
  bool write(uint8_t slot, const uint8_t* bytes, size_t n) { return ok_ && prefs_.putBytes(key(slot), bytes, n) == n; }
 private:
  static const char* key(uint8_t slot) { return slot ? "cfg-b" : "cfg-a"; }
  Preferences prefs_; bool ok_ = false;
};
class LocalController {
 public:
  LocalController() : bus_(io_), store_(nvs_), engine_(bus_, store_) {}
  static void bootReset() { McpWire::bootReset(); }
  void begin() {
    bootReset(); lock_ = xSemaphoreCreateMutex();
    if (!lock_) { engine_.abort("motion_lock"); return; }
    nvs_.begin();
    if (!Wire.begin(McpWire::kSda, McpWire::kScl, 400000)) { engine_.abort("i2c_begin"); return; }
    Wire.setTimeOut(3);
    engine_.begin(esp_random());
    if (xTaskCreatePinnedToCore(task, "flap-motion", 12288, this, 5, &task_, 1) != pdPASS) {
      engine_.abort("motion_task"); return;
    }
    esp_timer_create_args_t timer{}; timer.callback = notify; timer.arg = this;
    timer.dispatch_method = ESP_TIMER_TASK; timer.name = "flap-tick"; timer.skip_unhandled_events = true;
    if (esp_timer_create(&timer, &timer_) != ESP_OK || esp_timer_start_periodic(timer_, 250) != ESP_OK) {
      engine_.abort("motion_timer"); return;
    }
    running_ = true;
  }
  State snapshot() {
    if (!lock_) return engine_.state();
    xSemaphoreTake(lock_, portMAX_DELAY); State s = engine_.state(); xSemaphoreGive(lock_); return s;
  }
  sf::Error submit(const uint8_t* b, size_t n, uint32_t request, uint32_t& id) {
    if (!running_) return sf::Error::Readiness;
    xSemaphoreTake(lock_, portMAX_DELAY); const sf::Error e = engine_.submit(b, n, request, id); xSemaphoreGive(lock_);
    if (task_) xTaskNotifyGive(task_); return e;
  }
  bool pending() { if (!lock_) return false; xSemaphoreTake(lock_, portMAX_DELAY); const bool b = engine_.pending(); xSemaphoreGive(lock_); return b; }
  bool flash(bool hold) { if (!lock_) return false; xSemaphoreTake(lock_, portMAX_DELAY); const bool ok = engine_.flash(hold); xSemaphoreGive(lock_); return ok; }
  bool maintenance(bool enable) { if (!lock_) return false; xSemaphoreTake(lock_, portMAX_DELAY); const bool ok = engine_.maintenance(enable); xSemaphoreGive(lock_); return ok; }
  void configurationFailed(uint32_t id) { if (!lock_) return; xSemaphoreTake(lock_, portMAX_DELAY); engine_.configurationFailed(id); xSemaphoreGive(lock_); }
 private:
  static void notify(void* arg) { LocalController& c = *static_cast<LocalController*>(arg); if (c.task_) xTaskNotifyGive(c.task_); }
  static void task(void* arg) {
    LocalController& c = *static_cast<LocalController*>(arg);
    for (;;) {
      const uint32_t backlog = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      xSemaphoreTake(c.lock_, portMAX_DELAY);
      if (backlog > 16 && c.engine_.moving()) c.engine_.abort("timing_overrun");
      c.engine_.tick(); xSemaphoreGive(c.lock_);
    }
  }
  McpWire io_; McpBank<McpWire> bus_; MotionNvs nvs_; MotionStore<MotionNvs> store_;
  LocalEngine<McpBank<McpWire>, MotionStore<MotionNvs>> engine_;
  SemaphoreHandle_t lock_ = nullptr; TaskHandle_t task_ = nullptr; esp_timer_handle_t timer_ = nullptr;
  bool running_ = false;
};
}
