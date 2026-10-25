// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapLink.h>
#include "panel.h"
#include "storage.h"

namespace mega {
template <typename Backend> class Remote {
 public:
  Remote(Panel& panel, Backend& backend) : panel_(panel), store_(backend) {}
  void begin() {
    sf::Settings saved;
    if (store_.load(saved)) {
      panel_.applySettings(saved);
      if (store_.needsMigration()) store_.begin(saved);
    }
    boot_ = true;
  }
  bool saving() const { return store_.busy(); }
  bool owned() const { return owned_; }
  bool locked() const { return usbLocked_; }
  bool watching() const { return watching_; }
  bool consumeStopWatch() { const bool pending = stopUsbWatch_; stopUsbWatch_ = false; return pending; }
  bool saveLocal() {
    if (panel_.busy() || store_.busy()) return false;
    sf::Settings s; panel_.settings(s); return store_.begin(s);
  }
  bool loadLocal() {
    if (panel_.busy() || store_.busy()) return false;
    sf::Settings s;
    const bool ok = store_.load(s) && panel_.applySettings(s);
    if (ok) ++configRevision_;
    return ok;
  }
  // A real USB control command takes over; status/help do not. Never replay remote work.
  void usbActivity() {
    if (activeId_) finish(sf::Stage::Fault, sf::Error::UsbOverride);
    owned_ = false; watching_ = false; usbLocked_ = true; ++usbRevision_; ++configRevision_;
    requestSnapshot();
  }
  template <typename SerialPort> void poll(SerialPort& serial, uint32_t ms, uint32_t us, uint16_t rawHigh, bool usbWatching = false) {
    rawHigh_ = rawHigh;
    usbWatching_ = usbWatching;
    if (serial.available()) {
      sf::Packet p;
      for (uint8_t n = 0; n < 48 && serial.available(); ++n)
        if (decoder_.feed(uint8_t(serial.read()), ms, p)) receive(p, ms, us);
    }
    if (owned_ && uint32_t(ms - heartbeatAt_) > 2000) {
      panel_.stopAll(); owned_ = false; watching_ = false; linkError_ = "link_lost";
      leaseExpired_ = true;
      if (activeId_) finish(sf::Stage::Fault, sf::Error::LinkLost);
      requestSnapshot();
    }
    // EEPROM update can block for one byte (~3.3ms), and is never allowed during motion.
    if (store_.busy() && !panel_.busy()) store_.tick();
    if (activeId_ && !panel_.busy() && !store_.busy()) {
      const bool fault = panel_.error() || (activeSaving_ && store_.failed());
      finish(fault ? sf::Stage::Fault : sf::Stage::Done,
          !fault ? sf::Error::None : activeSaving_ && store_.failed() ? sf::Error::Storage : sf::Error::Readiness);
      if (fault) owned_ = false;
      requestSnapshot();
    }
    if (uint32_t(ms - snapshotAt_) >= 1000) { snapshotAt_ = ms; requestSnapshot(); }
    transmit(serial);
  }
 private:
  struct Reply { uint32_t id = 0; sf::Stage stage = sf::Stage::Rejected;
    sf::Error error = sf::Error::None; uint8_t op = 0; uint16_t hash = 0; };
  void requestSnapshot() {
    if (snapshotCursor_ > 11) { snapshotCursor_ = 0; ++snapshotId_; }
    else snapshotAgain_ = true;
  }
  void enqueue(const Reply& r) {
    if (replyCount_ == 4) { replyHead_ = (replyHead_ + 1) % 4; --replyCount_; }
    replies_[(replyHead_ + replyCount_) % 4] = r; ++replyCount_;
  }
  void reply(uint32_t id, sf::Stage stage, sf::Error error, uint8_t op, uint16_t hash, bool remember = true) {
    Reply r; r.id = id; r.stage = stage; r.error = error; r.op = op; r.hash = hash;
    if (remember) {
      uint8_t slot = 8;
      for (uint8_t i = 0; i < 8; ++i) if (cache_[i].id == id) { slot = i; break; }
      if (slot == 8) { slot = cacheCursor_; cacheCursor_ = (cacheCursor_ + 1) % 8; }
      cache_[slot] = r;
    }
    enqueue(r);
  }
  void finish(sf::Stage stage, sf::Error error) {
    reply(activeId_, stage, error, activeOp_, activeHash_);
    activeId_ = 0; activeSaving_ = false;
  }
  void receive(const sf::Packet& p, uint32_t ms, uint32_t us) {
    if (p.version != sf::kVersion) {
      if (p.kind == sf::Kind::Hello && p.session) { session_ = p.session; welcome_ = true; }
      return;  // Welcome advertises our version; no cross-version control.
    }
    if (p.kind == sf::Kind::Hello && p.session && p.length == 0) {
      if (session_ != p.session) {
        if (owned_) panel_.stopAll();
        owned_ = false; watching_ = false; activeId_ = 0; activeSaving_ = false;
        leaseExpired_ = false;
        for (uint8_t i = 0; i < 8; ++i) cache_[i].id = 0;
        replyHead_ = replyCount_ = 0; maxId_ = 0; session_ = p.session;
      }
      heartbeatAt_ = ms; welcome_ = true; requestSnapshot(); return;
    }
    if (p.session != session_ || !session_) return;
    if (p.kind == sf::Kind::Ping && p.length == 0) { heartbeatAt_ = ms; pong_ = true; return; }
    if (p.kind == sf::Kind::Query && p.length == 0) {
      if (p.id) {
        bool found = false;
        for (uint8_t i = 0; i < 8; ++i) if (cache_[i].id == p.id) { enqueue(cache_[i]); found = true; break; }
        if (!found) reply(p.id, sf::Stage::Rejected, sf::Error::Unknown, 0, 0, false);
      }
      requestSnapshot(); return;
    }
    if (p.kind != sf::Kind::Command || !p.id) return;
    const uint16_t hash = sf::crc16(p.data, p.length);
    for (uint8_t i = 0; i < 8; ++i) if (cache_[i].id == p.id) {
      if (cache_[i].hash == hash) enqueue(cache_[i]);
      else reply(p.id, sf::Stage::Rejected, sf::Error::Invalid, 0, hash, false);
      return;
    }
    if (maxId_ && int32_t(p.id - maxId_) <= 0) {
      reply(p.id, sf::Stage::Rejected, sf::Error::Unknown, 0, hash, false); return;
    }
    maxId_ = p.id;
    if (p.length < 2) { reply(p.id, sf::Stage::Rejected, sf::Error::Invalid, 0, hash); return; }
    const sf::Op op = sf::Op(p.data[0]);
    if (op == sf::Op::Stop && p.length == 2) {
      panel_.stopAll(); owned_ = false; watching_ = false; linkError_ = nullptr;
      stopUsbWatch_ = true; usbWatching_ = false;
      leaseExpired_ = false;
      if (activeId_) finish(sf::Stage::Fault, sf::Error::Stopped);
      reply(p.id, sf::Stage::Done, sf::Error::None, p.data[0], hash); requestSnapshot(); return;
    }
    if (panel_.busy() || store_.busy() || watching_ || activeId_) {
      reply(p.id, sf::Stage::Rejected, sf::Error::Busy, p.data[0], hash); return;
    }
    if (op == sf::Op::Claim && p.length == 2) {
      panel_.stopAll(); owned_ = false; usbLocked_ = false; linkError_ = nullptr;
      stopUsbWatch_ = true; usbWatching_ = false;
      leaseExpired_ = false;
      reply(p.id, sf::Stage::Done, sf::Error::None, p.data[0], hash); requestSnapshot(); return;
    }
    if (usbLocked_) { reply(p.id, sf::Stage::Rejected, sf::Error::UsbOverride, p.data[0], hash); return; }
    if (leaseExpired_) { reply(p.id, sf::Stage::Rejected, sf::Error::Session, p.data[0], hash); return; }
    const uint8_t unit = p.data[1]; const uint8_t* a = p.data + 2; const uint8_t n = p.length - 2;
    bool ok = false, setting = false, saves = false;
    const bool layout = op == sf::Op::Digits || op == sf::Op::Units || op == sf::Op::SetText ||
        op == sf::Op::Text || op == sf::Op::Save || op == sf::Op::Load || op == sf::Op::ApplyConfig;
    if (!layout && unit != sf::kAll && !panel_.enabled(unit)) {
      reply(p.id, sf::Stage::Rejected, sf::Error::Invalid, p.data[0], hash); return;
    }
    const uint16_t value = n == 2 ? sf::get16(a) : 0;
    if (unit == sf::kAll && !layout) {
      if (op == sf::Op::Arm && !n) ok = panel_.armAll();
      else if (op == sf::Op::Home && !n) ok = panel_.homeAll(us);
      else if (op == sf::Op::Cycle && !n) ok = panel_.cycleAll(us);
      else if (op == sf::Op::Speed && n == 2) { ok = panel_.speedAll(value); setting = true; }
      else if (op == sf::Op::Accel && n == 2) { ok = panel_.accelerationAll(value); setting = true; }
    } else if (layout) {
      if (op == sf::Op::Digits && n == 1) { ok = panel_.setDigits(a[0]); setting = true; }
      else if (op == sf::Op::Units && n >= 2 && n == a[0] + 1) { ok = panel_.setUnits(a + 1, a[0]); setting = true; }
      else if ((op == sf::Op::SetText || op == sf::Op::Text) && n >= 2 && n == a[0] + 1 && a[0] == panel_.digits()) {
        ok = op == sf::Op::SetText ? panel_.setCurrentText(a + 1, a[0]) : panel_.text(a + 1, a[0], us);
      } else if (op == sf::Op::Save && n == 0) { ok = saveLocal(); saves = true; }
      else if (op == sf::Op::Load && n == 0) { ok = loadLocal(); setting = true; }
      else if (op == sf::Op::ApplyConfig && n == sf::kSettingsSize) {
        sf::Settings s;
        ok = sf::decodeSettings(a, n, s) && panel_.applySettings(s) && store_.begin(s);
        setting = true; saves = true;
      }
    } else if (panel_.select(unit)) {
      Bench& b = panel_.selected(); Config c = b.config();
      switch (op) {
        case sf::Op::Select: ok = n == 0; break;
        case sf::Op::Arm: ok = !n && b.arm(); break;
        case sf::Op::SetIndex: ok = n == 1 && a[0] < 45 && b.setCurrent(a[0]); break;
        case sf::Op::Goto: ok = n == 1 && a[0] < 45 && b.goTo(a[0], us); break;
        case sf::Op::Step: ok = n == 2 && b.step(value, us); break;
        case sf::Op::Rev: ok = !n && b.step(c.revolution, us); break;
        case sf::Op::Flap: ok = !n && b.goTo((b.index() + 1) % 45, us); break;
        case sf::Op::Home: ok = !n && b.home(us); break;
        case sf::Op::Measure: ok = !n && b.measure(us); break;
        case sf::Op::Cycle: ok = !n && b.cycle(us); break;
        case sf::Op::Led: ok = !n && b.led(us); break;
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
      if (op == sf::Op::Polarity && ok) setting = true;
    }
    if (!ok) { reply(p.id, sf::Stage::Rejected, sf::Error::Readiness, p.data[0], hash); return; }
    linkError_ = nullptr; panel_.clearError(); heartbeatAt_ = ms;
    if (setting) ++configRevision_;
    if (op == sf::Op::Arm || panel_.busy() || watching_) owned_ = true;
    reply(p.id, sf::Stage::Accepted, sf::Error::None, p.data[0], hash);
    if (panel_.busy() || store_.busy()) {
      activeId_ = p.id; activeOp_ = p.data[0]; activeHash_ = hash; activeSaving_ = saves;
    } else reply(p.id, sf::Stage::Done, sf::Error::None, p.data[0], hash);
    requestSnapshot();
  }
  void snapshot(sf::Packet& p) {
    p.session = session_; p.id = snapshotId_;
    if (snapshotCursor_ == 0) {
      sf::PanelState s; s.count = panel_.digits(); s.selected = panel_.selectedUnit();
      for (uint8_t i = 0; i < s.count; ++i) s.mapping[i] = panel_.physicalUnit(i);
      s.busy = panel_.busy(); s.saving = store_.busy(); s.remote = owned_; s.usbLocked = usbLocked_; s.watching = watching_ || usbWatching_;
      s.usbRevision = usbRevision_; s.configRevision = configRevision_; s.faultUnit = panel_.faultUnit();
      const char* error = panel_.error() ? panel_.error() : linkError_;
      if (error) { strncpy(s.error, error, sizeof(s.error) - 1); }
      p.kind = sf::Kind::Panel; p.length = sf::kPanelSize; sf::encodePanel(s, p.data);
    } else if (snapshotCursor_ <= 10) {
      const uint8_t u = snapshotCursor_ - 1; const Bench& b = panel_.unit(u); const Config& c = b.config();
      sf::UnitState s; s.unit = u; s.enabled = panel_.enabled(u); s.mode = uint8_t(b.mode());
      s.reference = b.homed() ? 2 : b.manual() ? 1 : 0; s.armed = b.armed(); s.index = b.index(); s.target = b.target();
      s.rawHigh = (rawHigh_ & (1U << u)) != 0; s.hall = b.hall(); s.steps = b.steps(); s.remaining = b.remaining();
      s.measuredCount = b.measuredCount();
      for (uint8_t i = 0; i < s.measuredCount; ++i) s.measured[i] = b.measured(i);
      s.config.revolution = c.revolution; s.config.offset = c.offset; s.config.speed = c.intervalUs;
      s.config.accel = c.acceleration; s.config.direction = c.direction; memcpy(s.config.order, c.order, 4);
      s.config.activeLow = panel_.activeLow(u);
      const char* error = u == panel_.faultUnit() ? panel_.error() : b.error();
      if (error) { strncpy(s.error, error, sizeof(s.error) - 1); }
      p.kind = sf::Kind::Unit; p.length = sf::kUnitSize; sf::encodeUnit(s, p.data);
    } else {
      sf::Settings s; panel_.settings(s); p.kind = sf::Kind::Config; p.length = sf::kSettingsSize;
      sf::encodeSettings(s, p.data);
    }
    ++snapshotCursor_;
  }
  template <typename SerialPort> void transmit(SerialPort& serial) {
    if (txAt_ == txLength_) {
      sf::Packet p; p.session = session_;
      if (boot_) { p.kind = sf::Kind::Boot; p.session = 0; boot_ = false; }
      else if (replyCount_) {
        const Reply r = replies_[replyHead_]; replyHead_ = (replyHead_ + 1) % 4; --replyCount_;
        p.kind = sf::Kind::Result; p.id = r.id; p.length = 3;
        p.data[0] = uint8_t(r.stage); p.data[1] = uint8_t(r.error); p.data[2] = r.op;
      } else if (welcome_) { p.kind = sf::Kind::Welcome; p.length = 2; p.data[0] = sf::kVersion; p.data[1] = 1; welcome_ = false; }
      else if (pong_) { p.kind = sf::Kind::Pong; pong_ = false; }
      else if (session_ && (snapshotCursor_ <= 11 || snapshotAgain_)) {
        if (snapshotCursor_ > 11) { snapshotAgain_ = false; snapshotCursor_ = 0; ++snapshotId_; }
        snapshot(p);
      }
      else return;
      txAt_ = 0; txLength_ = sf::encode(p, tx_);
    }
    uint16_t count = txLength_ - txAt_;
    const int space = serial.availableForWrite();
    if (space <= 0) return;
    if (count > uint16_t(space)) count = uint16_t(space);
    if (count > 16) count = 16;
    txAt_ += serial.write(tx_ + txAt_, count);
  }
  Panel& panel_; SettingsStore<Backend> store_; sf::Decoder decoder_;
  Reply cache_[8], replies_[4]; uint8_t cacheCursor_ = 0, replyHead_ = 0, replyCount_ = 0;
  uint8_t tx_[sf::kWireSize]; uint16_t txAt_ = 0, txLength_ = 0, rawHigh_ = 0;
  uint32_t session_ = 0, maxId_ = 0, heartbeatAt_ = 0, snapshotAt_ = 0, snapshotId_ = 0;
  uint32_t activeId_ = 0, usbRevision_ = 0, configRevision_ = 0;
  uint16_t activeHash_ = 0; uint8_t activeOp_ = 0, snapshotCursor_ = 12;
  bool activeSaving_ = false, boot_ = false, welcome_ = false, pong_ = false;
  bool snapshotAgain_ = false;
  bool owned_ = false, usbLocked_ = false, watching_ = false;
  bool stopUsbWatch_ = false, usbWatching_ = false;
  bool leaseExpired_ = false;
  const char* linkError_ = nullptr;
};
}
