// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>
#include "bridge_engine.h"

namespace cam {
class Bridge {
 public:
  void begin() {
    lock_ = xSemaphoreCreateMutex();
    uart_.setRxBufferSize(1024); uart_.setTxBufferSize(512);
    uart_.begin(115200, SERIAL_8N1, 14, 13);
    engine_.begin(esp_random());
    xTaskCreatePinnedToCore(task, "mega-link", 8192, this, 2, &task_, 0);
  }
  State snapshot() {
    xSemaphoreTake(lock_, portMAX_DELAY); State s = engine_.state(); xSemaphoreGive(lock_); return s;
  }
  sf::Error submit(const uint8_t* bytes, size_t n, uint32_t request, uint32_t& id) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const sf::Error error = engine_.submit(bytes, n, request, millis(), id);
    xSemaphoreGive(lock_); return error;
  }
  bool pending() { xSemaphoreTake(lock_, portMAX_DELAY); bool b = engine_.pending(); xSemaphoreGive(lock_); return b; }
  void configurationFailed(uint32_t id) {
    xSemaphoreTake(lock_, portMAX_DELAY); engine_.configurationFailed(id); xSemaphoreGive(lock_);
  }
  bool maintenance(bool enable) {
    xSemaphoreTake(lock_, portMAX_DELAY); const bool ok = engine_.maintenance(enable); xSemaphoreGive(lock_); return ok;
  }
 private:
  static void task(void* arg) {
    Bridge& b = *static_cast<Bridge*>(arg);
    for (;;) {
      xSemaphoreTake(b.lock_, portMAX_DELAY); b.engine_.tick(b.uart_, millis()); xSemaphoreGive(b.lock_);
      vTaskDelay(1);
    }
  }
  HardwareSerial uart_{2}; BridgeEngine engine_; SemaphoreHandle_t lock_ = nullptr; TaskHandle_t task_ = nullptr;
};
}
