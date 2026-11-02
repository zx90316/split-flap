// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "bridge_engine.h"

namespace cam {
inline bool otaStopped(const State& s, uint32_t stopJob) {
  if (!stopJob || !s.connected || !s.valid || s.uncertain || s.protocolMismatch ||
      s.panel.busy || s.panel.saving || s.panel.watching || s.panel.usbLocked) return false;
  bool acknowledged = false;
  for (const auto& job : s.jobs)
    if (job.id == stopJob && job.stage == uint8_t(sf::Stage::Done) && job.error == sf::Error::None &&
        job.op == uint8_t(sf::Op::Stop)) acknowledged = true;
  for (const auto& unit : s.units) if (unit.armed) return false;
  return acknowledged;
}

// Flash writes can interrupt UART reception. A write is followed by a complete,
// newer snapshot before another write is allowed; the old stopped snapshot is
// never treated as sufficient evidence while waiting. No reconnect is accepted.
class OtaGuard {
 public:
  enum class Result { Ready, Waiting, LinkLost, MaintenanceLost, StopChanged, Timeout };
  void begin(const State& s, uint32_t stopJob) {
    stopJob_ = stopJob; session_ = s.session;
    usbRevision_ = s.panel.usbRevision; configRevision_ = s.panel.configRevision;
    waiting_ = freshRequired_ = false;
  }
  void afterWrite(const State& s, uint32_t now) {
    serial_ = s.snapshotSerial; waitAt_ = now; waiting_ = freshRequired_ = true;
  }
  Result check(const State& s, uint32_t now) {
    if (!s.connected || !s.valid || s.session != session_ || s.uncertain || s.protocolMismatch) return Result::LinkLost;
    if (!s.maintenance) return Result::MaintenanceLost;
    if (!otaStopped(s, stopJob_) || s.panel.usbRevision != usbRevision_ || s.panel.configRevision != configRevision_) return Result::StopChanged;
    if (uint32_t(now - s.updatedAt) > 1500 || (freshRequired_ && s.snapshotSerial == serial_)) {
      if (!waiting_) { waitAt_ = now; waiting_ = true; }
      return uint32_t(now - waitAt_) >= 2500 ? Result::Timeout : Result::Waiting;
    }
    waiting_ = freshRequired_ = false;
    return Result::Ready;
  }
 private:
  uint32_t stopJob_ = 0, session_ = 0, usbRevision_ = 0, configRevision_ = 0, serial_ = 0, waitAt_ = 0;
  bool waiting_ = false, freshRequired_ = false;
};
}
