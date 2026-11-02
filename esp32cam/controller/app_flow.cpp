// SPDX-License-Identifier: GPL-3.0-only
#include "app.h"
namespace cam {
void App::persistPaused() {
  if (!settings_.resumeDisplay) return;
  AppSettings candidate = settings_; candidate.resumeDisplay = false;
  if (store_.save(candidate)) settings_ = candidate; else storageOk_ = false;
}
bool App::beginFlow(Purpose purpose, const uint8_t* key, size_t size, uint32_t request) {
  const State s = bridge_.snapshot(); uint32_t id = 0; bool created = false;
  if (request && request < s.nextRequest) {
    const auto result = bridge_.reserve(key, size, request, id, created);
    if (result != sf::Error::None) error(409, sf::errorName(result));
    else { cJSON* o = cJSON_CreateObject(); num(o, "id", id); json(o, 202); }
    return false;
  }
  if (!s.connected || !s.valid || uint32_t(millis() - s.updatedAt) > 2000 || s.panel.usbLocked || !contiguous(s.settings)) {
    if (request) error(409, "Mega 尚未就緒，或安裝配置不是連續位置。"); return false;
  }
  if (request) { if (!idle(s)) return false; }
  else if (flow_.active() || pendingSettingsJob_ || s.maintenance || s.panel.busy || s.panel.saving || s.panel.watching || bridge_.pending()) return false;
  const auto result = bridge_.reserve(key, size, request, id, created);
  if (result != sf::Error::None) { if (request) error(409, sf::errorName(result)); return false; }
  flow_.begin(purpose, id, s.session); flowRemember_ = flowDynamic_ = false;
  if (s.uncertain) flow_.add(sf::Op::Claim);
  copyString("正在執行，關閉頁面仍會繼續。", flowMessage_, sizeof(flowMessage_));
  if (request) { cJSON* o = cJSON_CreateObject(); num(o, "id", id); json(o, 202); }
  return true;
}
void App::abortFlow() {
  recoveryPending_ = false;
  calJogReady_ = 0;
  if (flow_.active()) { bridge_.complete(flow_.parent(), sf::Error::Stopped); flow_.clear(); }
  copyString("已停止；需要重新操作才會繼續。", flowMessage_, sizeof(flowMessage_));
}
void App::captureDisplay(bool dynamic) {
  const State s = bridge_.snapshot();
  if (!s.connected || !s.valid || s.panel.busy || s.panel.saving) return;
  displayMemory_.observe(s.units); captureDynamic_ = dynamic;
}
void App::flowTick() {
  if (!flow_.active()) return;
  const State s = bridge_.snapshot(); flow_.tick(bridge_, s, millis());
  if (!flow_.finished()) return;
  auto result = flow_.error(); const Purpose purpose = flow_.purpose();
  if (result == sf::Error::None && purpose == Purpose::Recovery && !recoveryDisplayQueued_) {
    // Homing can take a minute. Resolve time/window only after it has finished.
    uint8_t bytes[13] = {uint8_t(sf::Op::Text), sf::kAll, s.settings.count};
    for (uint8_t i = 0; i < s.settings.count; ++i) { const auto glyph = displayMemory_.glyph(i); bytes[3 + i] = glyph < 45 ? glyph : 0; }
    if (settings_.resumeDisplay && programWindowOpen(settings_, time(nullptr))) {
      uint8_t target[10]; if (displayTarget(target)) memcpy(bytes + 3, target, s.settings.count);
    }
    flow_.add(bytes, 3 + s.settings.count); recoveryDisplayQueued_ = true;
    copyString("歸零完成，正在恢復顯示。", flowMessage_, sizeof(flowMessage_)); return;
  }
  if (result == sf::Error::None && purpose == Purpose::CalFinish) {
    AppSettings candidate = settings_;
    for (uint8_t i = 0; i < s.settings.count; ++i) {
      candidate.calibratedMask |= 1U << i; candidate.signatures[i] = geometrySignature(s.settings.units[i]);
    }
    if (!candidate.recoveryChosen) { candidate.autoRecovery = true; candidate.recoveryChosen = true; }
    if (store_.save(candidate)) { settings_ = candidate; storageOk_ = true; calibrating_ = false; }
    else { storageOk_ = false; result = sf::Error::Storage; }
  }
  if (result == sf::Error::None) {
    if (purpose == Purpose::CalStart || purpose == Purpose::CalFinish) calJogReady_ = (1U << s.settings.count) - 1;
    if (purpose == Purpose::CalCancel) calibrating_ = false;
    if (flowRemember_) captureDisplay(flowDynamic_);
    if (purpose == Purpose::Recovery || purpose == Purpose::StartDisplay) {
      running_ = settings_.resumeDisplay && strcmp(settings_.mode, "text");
      lastUsbRevision_ = s.panel.usbRevision; itemSince_ = 0;
      copyString(running_ ? "自動顯示執行中。" : "顯示已恢復。", pauseReason_, sizeof(pauseReason_));
    }
    copyString(purpose == Purpose::CalFinish ? "偏移已永久保存，校準完成。" : calibrating_ ? "歸零／微調完成，請確認空白頁後保存。" : "已完成。", flowMessage_, sizeof(flowMessage_));
  } else {
    calJogReady_ = 0;
    pause("流程已中止，請檢查畫面上的原因後重新操作。");
    snprintf(flowMessage_, sizeof(flowMessage_), "流程失敗：%s。已停止，不會自動重試。", sf::errorName(result));
    const uint8_t stop[] = {uint8_t(sf::Op::Stop), sf::kAll}; uint32_t id = 0;
    bridge_.submit(stop, sizeof(stop), 0, id);
  }
  bridge_.complete(flow_.parent(), result); flow_.clear();
}
void App::calibration() {
  cJSON* b = body(); if (!b) return;
  const State s = bridge_.snapshot(); uint32_t request = 0; int64_t unit = 0, value = 0;
  if (!requestId(b, s, request)) { cJSON_Delete(b); return; }
  const char* action = stringField(b, "action");
  const uint8_t code = !action ? 0 : !strcmp(action, "start") ? 1 : !strcmp(action, "preview") ? 2 :
    !strcmp(action, "jog") ? 3 : !strcmp(action, "finish") ? 4 : !strcmp(action, "cancel") ? 5 : 0;
  bool ok = code != 0;
  if (code == 2 || code == 3) {
    ok = fieldInteger(b, "unit", 0, s.settings.count - 1, unit) && fieldInteger(b, "value", 0, 19999, value);
    ok = ok && (code == 2 ? value < s.settings.units[unit].revolution : value == 1 || value == 10 || value == 100);
  }
  cJSON_Delete(b);
  if (!ok) { error(400, "校準操作或偏移步數無效。"); return; }
  uint8_t key[] = {240, code, uint8_t(unit), uint8_t(value), uint8_t(value >> 8)};
  if (request < s.nextRequest) { beginFlow(Purpose::Action, key, sizeof(key), request); return; }
  if ((code == 1 && calibrating_) || (code != 1 && !calibrating_)) { error(409, "請先開始歸零校準，或完成／取消目前校準。"); return; }
  if (code == 3 && !(calJogReady_ & (1U << unit))) { error(409, "請先套用數字並歸零預覽，再前進微調。"); return; }
  const Purpose p = code == 1 ? Purpose::CalStart : code == 2 ? Purpose::CalPreview : code == 3 ? Purpose::CalJog : code == 4 ? Purpose::CalFinish : Purpose::CalCancel;
  if (!beginFlow(p, key, sizeof(key), request)) return;
  pause("正在歸零校準。"); persistPaused();
  if (code == 1) {
    calibrationOriginal_ = s.settings; calibrating_ = true; flow_.add(sf::Op::Arm); flow_.add(sf::Op::Home);
  } else if (code == 2) {
    calJogReady_ |= 1U << unit; // Any failed preview clears this in flowTick.
    flow_.number(sf::Op::Offset, unit, value); flow_.add(sf::Op::Arm, unit); flow_.add(sf::Op::Home, unit);
  } else if (code == 3) {
    flow_.add(sf::Op::Arm, unit); flow_.number(sf::Op::Step, unit, value);
    flow_.number(sf::Op::Offset, unit, (s.settings.units[unit].offset + value) % s.settings.units[unit].revolution);
  } else if (code == 4) {
    flow_.add(sf::Op::Arm); flow_.add(sf::Op::Home); flow_.config(s.settings);
  } else flow_.config(calibrationOriginal_);
}
void App::recoveryTick() {
  if (!recoveryPending_) return;
  if (uint32_t(millis() - bootAt_) > 30000) {
    recoveryPending_ = false; copyString("復電等待連線逾時，請手動歸零。", flowMessage_, sizeof(flowMessage_)); return;
  }
  const State s = bridge_.snapshot();
  if (!s.connected || !s.valid || s.panel.busy || s.panel.saving || s.panel.watching || bridge_.pending() || flow_.active()) return;
  recoveryPending_ = false;
  if (!storageOk_ || !calibrated(settings_, s.settings) || s.panel.usbLocked) {
    copyString("復電自動復位未執行：請先完成全部位置校準。", flowMessage_, sizeof(flowMessage_)); return;
  }
  const uint8_t key[] = {241, 0};
  if (!beginFlow(Purpose::Recovery, key, sizeof(key), 0)) return;
  recoveryDisplayQueued_ = false;
  flow_.add(sf::Op::Arm); flow_.add(sf::Op::Home);
  copyString("復電復位中：歸零後恢復原顯示。", flowMessage_, sizeof(flowMessage_));
}
}
