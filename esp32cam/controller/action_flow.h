// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "bridge_engine.h"
namespace cam {
enum class Purpose : uint8_t { None, Action, CalStart, CalPreview, CalJog, CalFinish, CalCancel, Recovery, StartDisplay };
// A bounded sequence, not a queue of user actions. Each wire command is sent once.
class ActionFlow {
 public:
  void begin(Purpose purpose, uint32_t parent, uint32_t session) {
    *this = ActionFlow(); purpose_ = purpose; parent_ = parent; session_ = session; active_ = true;
  }
  bool add(const uint8_t* bytes, size_t n) {
    if (count_ == 6 || n < 2 || n > sf::kMaxPayload) return false;
    memcpy(steps_[count_], bytes, n); lengths_[count_++] = uint8_t(n); return true;
  }
  void add(sf::Op op, uint8_t unit = sf::kAll) { const uint8_t b[] = {uint8_t(op), unit}; add(b, 2); }
  void number(sf::Op op, uint8_t unit, uint16_t value) { uint8_t b[] = {uint8_t(op), unit, 0, 0}; sf::put16(b + 2, value); add(b, 4); }
  void config(const sf::Settings& s) { uint8_t b[sf::kSettingsSize + 2] = {uint8_t(sf::Op::ApplyConfig), sf::kAll}; sf::encodeSettings(s, b + 2); add(b, sizeof(b)); }
  bool active() const { return active_; }
  bool finished() const { return active_ && (error_ != sf::Error::None || at_ == count_); }
  sf::Error error() const { return error_; }
  uint32_t parent() const { return parent_; }
  Purpose purpose() const { return purpose_; }
  uint8_t step() const { return at_; }
  uint8_t count() const { return count_; }
  void clear() { active_ = false; }
  template <typename Transport> void tick(Transport& link, const State& s, uint32_t now) {
    if (!active_ || finished()) return;
    if (!s.connected || !s.valid || uint32_t(now - s.updatedAt) > 2000 || s.session != session_) { error_ = sf::Error::LinkLost; return; }
    if (s.panel.usbLocked) { error_ = sf::Error::UsbOverride; return; }
    if (child_) {
      const Job* job = nullptr; for (const auto& j : s.jobs) if (j.id == child_) job = &j;
      if (job && job->stage >= uint8_t(sf::Stage::Rejected)) { error_ = job->error; if (error_ == sf::Error::None) error_ = sf::Error::Unknown; return; }
      if (uint32_t(now - sentAt_) > 185000) { error_ = sf::Error::Unknown; return; }
      if (!job || job->stage != uint8_t(sf::Stage::Done)) return;
      if (!ackAt_) { ackAt_ = now; return; }
      // A result can precede the next panel snapshot. Never act on that old snapshot.
      if (int32_t(s.updatedAt - ackAt_) < 0 || s.panel.busy || s.panel.saving || link.pending()) return;
      child_ = ackAt_ = 0; ++at_;
      return;
    }
    if (s.panel.busy || s.panel.saving || link.pending()) return;
    const auto result = link.submit(steps_[at_], lengths_[at_], 0, now, child_);
    if (result != sf::Error::None) { if (result != sf::Error::Busy) error_ = result; return; }
    sentAt_ = now;
  }
 private:
  uint8_t steps_[6][sf::kMaxPayload] = {}, lengths_[6] = {}, count_ = 0, at_ = 0;
  uint32_t parent_ = 0, session_ = 0, child_ = 0, sentAt_ = 0, ackAt_ = 0;
  Purpose purpose_ = Purpose::None; sf::Error error_ = sf::Error::None; bool active_ = false;
};
}
