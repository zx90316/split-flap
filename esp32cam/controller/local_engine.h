// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "control_state.h"
#include <SplitFlapMotion.h>

namespace cam {
// Pure production controller. Bus/Store are injected for native fault and timing tests.
template <class Bus, class Store> class LocalEngine {
 public:
  LocalEngine(Bus& bus, Store& store) : bus_(bus), store_(store) {}
  void begin(uint32_t seed) {
    state_.boot = seed ? seed : 1; state_.valid = true;
    sf::Settings s; bool hall = false;
    if (store_.load(s, hall)) { panel_.applySettings(s); hall_ = hall; }
    state_.configStorageOk = store_.healthy();
    initialize(); sync();
  }
  const State& state() const { return state_; }
  bool pending() const { return queued_ || active_ || saving_ || recover_; }
  bool moving() const { return panel_.busy(); }
  void abort(const char* reason) { fault(reason); sync(); }
  bool flash(bool hold) {
    if (hold && (pending() || panel_.busy() || watching_ || state_.maintenance || !bus_.off())) return false;
    saving_ = hold; sync(); return true;
  }
  bool maintenance(bool enable) {
    if (enable) {
      if (pending() || panel_.busy() || watching_ || !bus_.off()) return false;
      for (uint8_t i = 0; i < panel_.digits(); ++i) if (panel_.unit(panel_.physicalUnit(i)).armed()) return false;
      bus_.disable(); state_.maintenance = true;
    } else if (state_.maintenance) { state_.maintenance = false; recover_ = true; }
    sync(); return true;
  }
  void configurationFailed(uint32_t id) { mark(id, sf::Stage::Fault, sf::Error::Storage); }
  sf::Error submit(const uint8_t* bytes, size_t n, uint32_t request, uint32_t& id) {
    if (!bytes || n < 2 || n > sf::kMaxPayload) return sf::Error::Invalid;
    const uint16_t hash = sf::crc16(bytes, n);
    if (request && request < state_.nextRequest) {
      for (const auto& j : state_.jobs) if (j.id && j.request == request) {
        if (j.hash != hash) return sf::Error::Invalid;
        id = j.id; return sf::Error::None;
      }
      return sf::Error::Unknown;
    }
    if (request && request != state_.nextRequest) return sf::Error::Invalid;
    const sf::Op op = sf::Op(bytes[0]); const bool stop = op == sf::Op::Stop || op == sf::Op::Claim;
    if (saving_ || state_.maintenance || (!stop && (queued_ || active_ || panel_.busy() || watching_))) return sf::Error::Busy;
    if (stop) {
      if (queued_) mark(queuedId_, sf::Stage::Fault, sf::Error::Stopped);
      if (active_) mark(active_, sf::Stage::Fault, sf::Error::Stopped);
      active_ = 0;
    }
    id = ++nextId_; if (!id) id = ++nextId_;
    if (request) ++state_.nextRequest;
    Job& j = state_.jobs[jobCursor_]; jobCursor_ = (jobCursor_ + 1) % 16;
    j = Job(); j.id = id; j.request = request; j.hash = hash; j.op = bytes[0];
    memcpy(command_, bytes, n); commandSize_ = uint8_t(n); queuedId_ = id; queued_ = true; sync();
    return sf::Error::None;
  }
  void tick() {
    const uint32_t now = bus_.nowUs();
    if (lastTickKnown_) {
      const uint32_t gap = now - lastTick_;
      if (panel_.busy()) {
        if (gap > state_.maxTickGapUs) state_.maxTickGapUs = gap;
        if (gap > 800) ++state_.lateTicks;
        if (gap > 100000) fault("loop_stall");
      }
    }
    lastTick_ = now; lastTickKnown_ = true;
    if (recover_) { recover_ = false; initialize(); }
    if (queued_ && !saving_ && !state_.maintenance) execute();
    if (!state_.maintenance && !saving_ && bus_.ready()) {
      // Hall sampling is independent of motion. Never wait for a new Hall sample to step.
      if (hall_ && uint32_t(bus_.nowUs() - hallAt_) >= 2000) {
        if (!bus_.halls(rawHigh_)) fault(bus_.error());
        hallAt_ = bus_.nowUs();
      }
      if (bus_.ready()) {
        panel_.tick(bus_.nowUs(), rawHigh_, bus_.allowSteps(panel_));
        if (panel_.error()) fault(panel_.error());
        else if (!bus_.flush(panel_)) fault(bus_.error());
      }
      if (bus_.ready() && uint32_t(bus_.nowUs() - auditAt_) >= 20000) {
        auditAt_ = bus_.nowUs(); if (!bus_.auditOne()) fault(bus_.error());
      }
      if (active_ && !panel_.busy() && bus_.ready()) {
        if (!bus_.auditAll()) fault(bus_.error());
        else { mark(active_, sf::Stage::Done, sf::Error::None); active_ = 0; }
      }
    }
    sync();
  }
 private:
  void mark(uint32_t id, sf::Stage stage, sf::Error error) {
    for (auto& j : state_.jobs) if (j.id == id) { j.stage = uint8_t(stage); j.error = error; return; }
  }
  void fault(const char* reason) {
    const char* text = reason ? reason : "i2c_fault";
    strncpy(error_, text, sizeof(error_) - 1); error_[sizeof(error_) - 1] = 0;
    faultUnit_ = panel_.faultUnit();
    bus_.disable(); panel_.stopAll(); watching_ = false;
    if (active_) { mark(active_, sf::Stage::Fault, sf::Error::Readiness); active_ = 0; }
  }
  bool initialize() {
    sf::Settings s; panel_.settings(s);
    if (!bus_.begin(Bus::required(s, hall_))) { fault(bus_.error()); return false; }
    error_[0] = 0; faultUnit_ = 10; hallAt_ = auditAt_ = bus_.nowUs(); return true;
  }
  bool save(const sf::Settings& s, bool hall) {
    saving_ = true; const bool ok = store_.save(s, hall); saving_ = false;
    state_.configStorageOk = ok; return ok;
  }
  bool apply(const sf::Settings& s, bool hall, bool persist) {
    if (!sf::validSettings(s)) return false;
    if (persist && !save(s, hall)) return false;
    const bool reset = !bus_.ready() || Bus::required(s, hall) != bus_.requiredMask() || hall != hall_;
    panel_.applySettings(s); hall_ = hall;
    if (reset) {
      for (uint8_t u = 0; u < 10; ++u) {
        panel_.unit(u).disarm();
        if (!hall_ && panel_.unit(u).homed()) panel_.unit(u).stop();
      }
      initialize();
    }
    return true;  // A valid installation can be saved before all hardware is attached.
  }
  void execute() {
    const uint32_t id = queuedId_; queued_ = false;
    const sf::Op op = sf::Op(command_[0]); const uint8_t unit = command_[1];
    const uint8_t* a = command_ + 2; const uint8_t n = commandSize_ - 2;
    const uint16_t value = n == 2 ? sf::get16(a) : 0;
    bool ok = false; sf::Error failure = sf::Error::Readiness;
    mark(id, sf::Stage::Accepted, sf::Error::None);
    if (op == sf::Op::Stop || op == sf::Op::Claim) {
      panel_.stopAll(); watching_ = false;
      if (bus_.ready() && !bus_.flush(panel_)) fault(bus_.error());
      else if (!bus_.ready()) bus_.disable();
      ok = !n;
    } else if (op == sf::Op::Init && !n) { panel_.stopAll(); watching_ = false; ok = initialize(); }
    else if (op == sf::Op::ApplyConfig && (n == sf::kSettingsSize || n == sf::kSettingsSize + 1)) {
      sf::Settings s; const bool hall = n == sf::kSettingsSize + 1 ? a[sf::kSettingsSize] != 0 : hall_;
      ok = (n == sf::kSettingsSize || a[sf::kSettingsSize] <= 1) &&
          sf::decodeSettings(a, sf::kSettingsSize, s) && apply(s, hall, true);
      failure = state_.configStorageOk ? sf::Error::Invalid : sf::Error::Storage;
    } else if ((op == sf::Op::Save || op == sf::Op::Load) && !n) {
      sf::Settings s; bool hall = hall_; panel_.settings(s);
      ok = op == sf::Op::Save ? save(s, hall) : store_.load(s, hall) && apply(s, hall, false);
      failure = sf::Error::Storage;
    } else if (op == sf::Op::Digits && n == 1) {
      sf::Settings s; panel_.settings(s); s.count = a[0];
      for (uint8_t i = 0; i < 10; ++i) s.mapping[i] = i;
      ok = apply(s, hall_, false);
    } else if (op == sf::Op::Units && n >= 2 && n == a[0] + 1 && a[0] <= 10) {
      sf::Settings s; panel_.settings(s); s.count = a[0]; memcpy(s.mapping, a + 1, s.count); ok = apply(s, hall_, false);
    } else if (bus_.ready()) {
      if ((op == sf::Op::Home || op == sf::Op::Measure || op == sf::Op::Watch) && !hall_) ok = false;
      else if ((op == sf::Op::SetText || op == sf::Op::Text) && n == panel_.digits() + 1 && a[0] == panel_.digits())
        ok = op == sf::Op::SetText ? panel_.setCurrentText(a + 1, a[0]) : panel_.text(a + 1, a[0], bus_.nowUs());
      else if (unit == sf::kAll) {
        if (op == sf::Op::Arm && !n) ok = panel_.armAll();
        else if (op == sf::Op::Home && !n) ok = panel_.homeAll(bus_.nowUs());
        else if (op == sf::Op::Cycle && !n) ok = panel_.cycleAll(bus_.nowUs());
        else if (op == sf::Op::Speed && n == 2) ok = panel_.speedAll(value);
        else if (op == sf::Op::Accel && n == 2) ok = panel_.accelerationAll(value);
      } else if (panel_.select(unit)) {
        mega::Bench& b = panel_.selected(); mega::Config c = b.config(); bool setting = false;
        switch (op) {
          case sf::Op::Select: ok = !n; break;
          case sf::Op::Arm: ok = !n && b.arm(); break;
          case sf::Op::SetIndex: ok = n == 1 && b.setCurrent(a[0]); break;
          case sf::Op::Goto: ok = n == 1 && b.goTo(a[0], bus_.nowUs()); break;
          case sf::Op::Step: ok = n == 2 && b.step(value, bus_.nowUs()); break;
          case sf::Op::Rev: ok = !n && b.step(c.revolution, bus_.nowUs()); break;
          case sf::Op::Flap: ok = !n && b.goTo((b.index() + 1) % 45, bus_.nowUs()); break;
          case sf::Op::Home: ok = !n && b.home(bus_.nowUs()); break;
          case sf::Op::Measure: ok = !n && b.measure(bus_.nowUs()); break;
          case sf::Op::Cycle: ok = !n && b.cycle(bus_.nowUs()); break;
          case sf::Op::Led: ok = !n && b.led(bus_.nowUs()); break;
          case sf::Op::Watch: ok = !n; if (ok) watching_ = true; break;
          case sf::Op::Speed: if (n == 2) { c.intervalUs = value; setting = true; } break;
          case sf::Op::Accel: if (n == 2) { c.acceleration = value; setting = true; } break;
          case sf::Op::Revolution: if (n == 2) { c.revolution = value; setting = true; } break;
          case sf::Op::Offset: if (n == 2) { c.offset = value; setting = true; } break;
          case sf::Op::Direction: if (n == 1) { c.direction = int8_t(a[0]); setting = true; } break;
          case sf::Op::Order: if (n == 4) { memcpy(c.order, a, 4); setting = true; } break;
          case sf::Op::Polarity: ok = n == 1 && a[0] <= 1 && panel_.setPolarity(a[0]); break;
          default: break;
        }
        if (setting) ok = b.configure(c);
      }
    }
    if (!ok) { mark(id, sf::Stage::Rejected, failure); return; }
    if (bus_.ready() && !bus_.flush(panel_)) { fault(bus_.error()); mark(id, sf::Stage::Fault, sf::Error::Readiness); return; }
    if (panel_.busy()) active_ = id; else mark(id, sf::Stage::Done, sf::Error::None);
  }
  void sync() {
    state_.updatedAt = bus_.nowMs(); state_.connected = bus_.ready(); state_.initialized = bus_.ready();
    state_.outputsDisabled = bus_.off(); state_.hallEnabled = hall_; state_.presentMask = bus_.present();
    state_.requiredMask = bus_.requiredMask(); state_.maxFlushUs = bus_.maxFlushUs(); panel_.settings(state_.settings);
    sf::PanelState& p = state_.panel; p.count = panel_.digits(); p.selected = panel_.selectedUnit();
    p.busy = queued_ || panel_.busy(); p.saving = saving_; p.watching = watching_;
    p.faultUnit = faultUnit_; memset(p.error, 0, sizeof(p.error));
    if (error_[0]) strncpy(p.error, error_, sizeof(p.error) - 1);
    for (uint8_t i = 0; i < p.count; ++i) p.mapping[i] = panel_.physicalUnit(i);
    for (uint8_t u = 0; u < 10; ++u) {
      const mega::Bench& b = panel_.unit(u); sf::UnitState& s = state_.units[u];
      s.unit = u; s.enabled = panel_.enabled(u); s.armed = b.armed(); s.index = b.index(); s.target = b.target();
      s.reference = uint8_t(b.homed() ? 2 : b.manual() ? 1 : 0); s.mode = uint8_t(b.mode());
      s.rawHigh = (rawHigh_ & (1U << u)) != 0; s.hall = b.hall(); s.steps = b.steps(); s.remaining = b.remaining();
      s.config = state_.settings.units[u]; s.measuredCount = b.measuredCount(); memset(s.error, 0, sizeof(s.error));
      if (s.enabled && error_[0] && (faultUnit_ == 10 || faultUnit_ == u)) strncpy(s.error, error_, sizeof(s.error) - 1);
      for (uint8_t i = 0; i < s.measuredCount; ++i) s.measured[i] = b.measured(i);
    }
  }
  Bus& bus_; Store& store_; mega::Panel panel_; State state_;
  bool hall_ = false, watching_ = false, queued_ = false, saving_ = false, lastTickKnown_ = false, recover_ = false;
  uint32_t active_ = 0, queuedId_ = 0, nextId_ = 0, lastTick_ = 0, hallAt_ = 0, auditAt_ = 0;
  uint16_t rawHigh_ = 0x3ff;
  uint8_t command_[sf::kMaxPayload] = {}, commandSize_ = 0, jobCursor_ = 0, faultUnit_ = 10;
  char error_[24] = {};
};
}
