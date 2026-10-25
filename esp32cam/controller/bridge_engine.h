// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapLink.h>

namespace cam {
struct Job {
  uint32_t id = 0, request = 0;
  uint16_t hash = 0;
  uint8_t op = 0, stage = 0;  // 0 queued; otherwise sf::Stage
  sf::Error error = sf::Error::None;
};
struct State {
  bool connected = false, valid = false, protocolMismatch = false, uncertain = false, maintenance = false;
  uint32_t boot = 0, session = 0, updatedAt = 0, nextRequest = 1;
  sf::PanelState panel;
  sf::UnitState units[10];
  sf::Settings settings;
  Job jobs[16];
};
class BridgeEngine {
 public:
  void begin(uint32_t seed) { state_.boot = seed ? seed : 1; rotateSession(); }
  const State& state() const { return state_; }
  bool pending() const { return activeId_ != 0; }
  // A config import also saves ESP preferences after Mega commits its half of the job.
  void configurationFailed(uint32_t id) {
    for (auto& j : state_.jobs) if (j.id == id && j.op == uint8_t(sf::Op::ApplyConfig) && j.stage == uint8_t(sf::Stage::Done)) {
      j.stage = uint8_t(sf::Stage::Fault); j.error = sf::Error::Storage;
    }
  }
  // External request numbers are strictly monotonic per ESP boot. Retired IDs can never run again.
  sf::Error submit(const uint8_t* payload, size_t n, uint32_t request, uint32_t now, uint32_t& id) {
    if (!payload || n < 2 || n > sf::kMaxPayload) return sf::Error::Invalid;
    const uint16_t hash = sf::crc16(payload, n);
    if (request && request < state_.nextRequest) {
      for (const auto& j : state_.jobs) if (j.id && j.request == request) {
        if (j.hash != hash) return sf::Error::Invalid;
        id = j.id; return sf::Error::None;
      }
      return sf::Error::Unknown;
    }
    if (request && request != state_.nextRequest) return sf::Error::Invalid;
    const bool stop = payload[0] == uint8_t(sf::Op::Stop);
    if (!state_.connected || !state_.valid || uint32_t(now - state_.updatedAt) > 2000) return sf::Error::LinkLost;
    if (state_.maintenance || (!stop && (activeId_ || state_.panel.busy || state_.panel.saving || state_.panel.watching))) return sf::Error::Busy;
    if (!stop && payload[0] != uint8_t(sf::Op::Claim) && state_.uncertain) return sf::Error::Unknown;
    if (!stop && payload[0] != uint8_t(sf::Op::Claim) && state_.panel.usbLocked) return sf::Error::UsbOverride;
    if (stop && activeId_) mark(activeId_, sf::Stage::Fault, sf::Error::Stopped);
    if (request) ++state_.nextRequest;
    id = ++wireId_; if (!id) id = ++wireId_;
    Job& j = state_.jobs[jobCursor_]; jobCursor_ = (jobCursor_ + 1) % 16;
    j = Job(); j.id = id; j.request = request; j.hash = hash; j.op = payload[0];
    activeId_ = id; command_ = sf::Packet(); command_.kind = sf::Kind::Command;
    command_.session = state_.session; command_.id = id; command_.length = uint8_t(n);
    memcpy(command_.data, payload, n); commandPending_ = true; queryAt_ = now; sentAt_ = now; return sf::Error::None;
  }
  bool maintenance(bool enable) {
    if (enable) {
      if (!state_.connected || !state_.valid || activeId_ || state_.panel.busy || state_.panel.saving || state_.panel.watching) return false;
      for (uint8_t i = 0; i < state_.panel.count; ++i) if (state_.units[state_.panel.mapping[i]].armed) return false;
    }
    state_.maintenance = enable; return true;
  }
  template <typename Port> void tick(Port& port, uint32_t now) {
    for (uint8_t budget = 0; budget < 96 && port.available(); ++budget) {
      sf::Packet p; if (decoder_.feed(uint8_t(port.read()), now, p)) receive(p, now);
    }
    if (state_.connected && uint32_t(now - lastRx_) > 2000) disconnect(sf::Error::LinkLost);
    // Mega allows 180 seconds per unit; a sequential ten-unit job may take 30 minutes.
    if (activeId_ && uint32_t(now - sentAt_) > 1805000) {
      mark(activeId_, sf::Stage::Fault, sf::Error::Unknown); activeId_ = 0; state_.uncertain = true; commandPending_ = false;
    }
    if (txAt_ == txLength_) {
      sf::Packet p; p.session = state_.session; bool send = true;
      if (!state_.connected) {
        if (uint32_t(now - helloAt_) >= 1000 || !helloSent_) {
          p.kind = sf::Kind::Hello; helloAt_ = now; helloSent_ = true;
        } else send = false;
      } else if (commandPending_) { p = command_; commandPending_ = false; sentAt_ = now; }
      else if (uint32_t(now - pingAt_) >= 500) { p.kind = sf::Kind::Ping; pingAt_ = now; }
      else if (uint32_t(now - queryAt_) >= 500) { p.kind = sf::Kind::Query; p.id = activeId_; queryAt_ = now; }
      else send = false;
      if (send) { txAt_ = 0; txLength_ = sf::encode(p, tx_); }
    }
    if (txAt_ < txLength_) {
      const int space = port.availableForWrite(); if (space <= 0) return;
      size_t n = txLength_ - txAt_; if (n > size_t(space)) n = size_t(space); if (n > 32) n = 32;
      txAt_ += port.write(tx_ + txAt_, n);
    }
  }
  const Job* job(uint32_t id) const { for (const auto& j : state_.jobs) if (j.id == id) return &j; return nullptr; }
 private:
  void rotateSession() {
    nonce_ = (nonce_ ? nonce_ : state_.boot) * 1664525U + 1013904223U;
    state_.session = nonce_ ? nonce_ : 1; helloSent_ = false; snapshotId_ = 0;
    snapshotMask_ = 0; havePanel_ = haveConfig_ = false;
  }
  void mark(uint32_t id, sf::Stage stage, sf::Error error) {
    for (auto& j : state_.jobs) if (j.id == id) {
      if (j.stage >= uint8_t(sf::Stage::Done)) return;
      j.stage = uint8_t(stage); j.error = error; return;
    }
  }
  void disconnect(sf::Error reason) {
    if (activeId_) mark(activeId_, sf::Stage::Fault, reason);
    activeId_ = 0; commandPending_ = false; state_.connected = false; state_.valid = false;
    state_.maintenance = false; state_.uncertain = true; rotateSession();
  }
  void receive(const sf::Packet& p, uint32_t now) {
    if (p.version != sf::kVersion) { if (state_.connected) disconnect(sf::Error::Protocol); state_.protocolMismatch = true; return; }
    if (p.kind == sf::Kind::Boot && p.session == 0) { if (state_.connected) disconnect(sf::Error::Session); return; }
    if (p.session != state_.session) return;
    if (p.kind == sf::Kind::Welcome) {
      if (p.length != 2 || p.data[0] != sf::kVersion) {
        if (state_.connected) disconnect(sf::Error::Protocol);
        state_.protocolMismatch = true; return;
      }
      state_.connected = true; state_.protocolMismatch = false; lastRx_ = now;
      // New sessions require an explicit STOP or Claim before starting another job.
      return;
    }
    if (!state_.connected) return;
    lastRx_ = now;
    if (p.kind == sf::Kind::Result && p.length == 3) {
      const sf::Stage stage = sf::Stage(p.data[0]); const sf::Error error = sf::Error(p.data[1]);
      if (p.data[0] < 1 || p.data[0] > 4) return;
      mark(p.id, stage, error);
      if (p.id == activeId_ && stage != sf::Stage::Accepted) {
        if (error == sf::Error::Unknown || error == sf::Error::LinkLost || error == sf::Error::Session || error == sf::Error::Protocol) state_.uncertain = true;
        if (stage == sf::Stage::Done && (p.data[2] == uint8_t(sf::Op::Stop) || p.data[2] == uint8_t(sf::Op::Claim))) state_.uncertain = false;
        activeId_ = 0;
      }
      return;
    }
    if (p.kind != sf::Kind::Panel && p.kind != sf::Kind::Unit && p.kind != sf::Kind::Config) return;
    if (snapshotId_ != p.id) { snapshotId_ = p.id; snapshotMask_ = 0; havePanel_ = haveConfig_ = false; }
    if (p.kind == sf::Kind::Panel) havePanel_ = sf::decodePanel(p.data, p.length, assemblingPanel_);
    else if (p.kind == sf::Kind::Unit) {
      sf::UnitState s; if (sf::decodeUnit(p.data, p.length, s)) { assemblingUnits_[s.unit] = s; snapshotMask_ |= 1U << s.unit; }
    } else haveConfig_ = sf::decodeSettings(p.data, p.length, assemblingSettings_);
    if (havePanel_ && haveConfig_ && snapshotMask_ == 0x3ff) {
      if (assemblingPanel_.count != assemblingSettings_.count ||
          memcmp(assemblingPanel_.mapping, assemblingSettings_.mapping, assemblingPanel_.count)) return;
      state_.panel = assemblingPanel_; state_.settings = assemblingSettings_;
      if (!strcmp(state_.panel.error, "link_lost")) state_.uncertain = true;
      memcpy(state_.units, assemblingUnits_, sizeof(state_.units)); state_.updatedAt = now; state_.valid = true;
    }
  }
  State state_; sf::Decoder decoder_; sf::Packet command_;
  sf::PanelState assemblingPanel_; sf::UnitState assemblingUnits_[10]; sf::Settings assemblingSettings_;
  uint8_t tx_[sf::kWireSize]; size_t txAt_ = 0, txLength_ = 0;
  uint32_t nonce_ = 0, wireId_ = 0, activeId_ = 0, lastRx_ = 0, helloAt_ = 0, pingAt_ = 0, queryAt_ = 0, sentAt_ = 0, snapshotId_ = 0;
  uint16_t snapshotMask_ = 0; uint8_t jobCursor_ = 0;
  bool commandPending_ = false, helloSent_ = false, havePanel_ = false, haveConfig_ = false;
};
}
