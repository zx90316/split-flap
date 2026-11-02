// SPDX-License-Identifier: GPL-3.0-only
#include "panel.h"

namespace mega {
Panel::Panel() {
  for (uint8_t u = 0; u < kMaxUnits; ++u) polarity_[u] = true;
}

bool Panel::enabled(uint8_t unit) const {
  return unit < kMaxUnits && (enabledMask_ & (1U << unit));
}

void Panel::settings(sf::Settings& out) const {
  out.count = count_;
  for (uint8_t i = 0; i < 10; ++i) {
    out.mapping[i] = i < count_ ? mapping_[i] : 0;
    const Config& c = units_[i].config(); sf::Calibration& d = out.units[i];
    d.revolution = c.revolution; d.offset = c.offset; d.speed = c.intervalUs;
    d.accel = c.acceleration; d.direction = c.direction; d.activeLow = polarity_[i];
    memcpy(d.order, c.order, 4);
  }
}

bool Panel::applySettings(const sf::Settings& settings) {
  if (busy() || !sf::validSettings(settings)) return false;
  for (uint8_t u = 0; u < 10; ++u) {
    const sf::Calibration& d = settings.units[u]; Config c;
    c.revolution = d.revolution; c.offset = d.offset; c.intervalUs = d.speed;
    c.acceleration = d.accel; c.direction = d.direction; memcpy(c.order, d.order, 4);
    units_[u].configure(c);
    if (polarity_[u] != d.activeLow) units_[u].stop();
    polarity_[u] = d.activeLow;
  }
  return setUnits(settings.mapping, settings.count);
}

bool Panel::busy() const {
  if (batching()) return true;
  for (uint8_t u = 0; u < kMaxUnits; ++u) if (units_[u].busy()) return true;
  return false;
}

void Panel::stopAll() {
  batch_ = Batch::None;
  cursor_ = 0;
  error_ = nullptr;
  faultUnit_ = kMaxUnits;
  for (uint8_t u = 0; u < kMaxUnits; ++u) units_[u].stop();
}

bool Panel::setDigits(uint8_t count) {
  if (count == 0 || count > kMaxUnits) return false;
  uint8_t mapping[kMaxUnits];
  for (uint8_t i = 0; i < count; ++i) mapping[i] = i;
  return setUnits(mapping, count);
}

bool Panel::setUnits(const uint8_t* units, uint8_t count) {
  if (!units || count == 0 || count > kMaxUnits || busy()) return false;
  uint16_t seen = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (units[i] >= kMaxUnits || (seen & (1U << units[i]))) return false;
    seen |= 1U << units[i];
  }
  if (count_ == count && !memcmp(mapping_, units, count)) return true;
  for (uint8_t u = 0; u < kMaxUnits; ++u) {
    if ((enabledMask_ & seen) & (1U << u)) units_[u].disarm();
    else units_[u].stop();
  }
  batch_ = Batch::None; cursor_ = 0; error_ = nullptr; faultUnit_ = kMaxUnits;
  count_ = count;
  enabledMask_ = seen;
  for (uint8_t i = 0; i < count; ++i) mapping_[i] = units[i];
  if (!enabled(selected_)) selected_ = mapping_[0];
  return true;
}

bool Panel::select(uint8_t unit) {
  if (busy() || !enabled(unit)) return false;
  selected_ = unit;  // Switching selection preserves each unit's calibration and reference.
  return true;
}

bool Panel::setPolarity(bool activeLow) {
  if (busy()) return false;
  if (polarity_[selected_] != activeLow) selected().stop();
  polarity_[selected_] = activeLow;
  return true;
}

bool Panel::speedAll(uint16_t interval) {
  if (busy() || interval < Bench::kMinIntervalUs || interval > Bench::kMaxIntervalUs) return false;
  for (uint8_t i = 0; i < count_; ++i) {
    Config c = units_[mapping_[i]].config();
    c.intervalUs = interval;
    units_[mapping_[i]].configure(c);  // Other fields were already validated.
  }
  return true;
}

bool Panel::accelerationAll(uint16_t acceleration) {
  if (busy() || acceleration < 100 || acceleration > 6000) return false;
  for (uint8_t i = 0; i < count_; ++i) {
    Config c = units_[mapping_[i]].config();
    c.acceleration = acceleration;
    units_[mapping_[i]].configure(c);
  }
  return true;
}

bool Panel::armAll() {
  if (busy()) return false;
  error_ = nullptr;
  faultUnit_ = kMaxUnits;
  for (uint8_t i = 0; i < count_; ++i) units_[mapping_[i]].arm();
  return true;
}

bool Panel::setCurrentText(const uint8_t* values, uint8_t count) {
  if (!values || count != count_ || busy()) return false;
  for (uint8_t i = 0; i < count_; ++i) if (values[i] >= kFlapCount) return false;
  for (uint8_t i = 0; i < count_; ++i) units_[mapping_[i]].setCurrent(values[i]);
  error_ = nullptr;
  faultUnit_ = kMaxUnits;
  return true;
}

bool Panel::ready(bool needsPosition) const {
  if (busy()) return false;
  for (uint8_t i = 0; i < count_; ++i) {
    const Bench& b = units_[mapping_[i]];
    if (!b.armed() || (needsPosition && !b.positioned())) return false;
  }
  return true;
}

bool Panel::text(const uint8_t* values, uint8_t count, uint32_t now) {
  if (!values || count != count_ || !ready(true)) return false;
  for (uint8_t i = 0; i < count_; ++i) if (values[i] >= kFlapCount) return false;
  for (uint8_t i = 0; i < count_; ++i) targets_[i] = values[i];
  error_ = nullptr;
  faultUnit_ = kMaxUnits;
  batch_ = Batch::Text;
  cursor_ = 0;
  startBatch(now);
  return true;
}

bool Panel::homeAll(uint32_t now) {
  if (!ready(false)) return false;
  error_ = nullptr;
  faultUnit_ = kMaxUnits;
  batch_ = Batch::Home;
  cursor_ = 0;
  startBatch(now);
  return true;
}

bool Panel::cycleAll(uint32_t now) {
  if (!ready(true)) return false;
  error_ = nullptr;
  faultUnit_ = kMaxUnits;
  batch_ = Batch::Cycle;
  cursor_ = 0;
  startBatch(now);
  return true;
}

void Panel::fail(uint8_t unit, const char* reason) {
  stopAll();
  error_ = reason;
  faultUnit_ = unit;
}

void Panel::startBatch(uint32_t now) {
  bool started = false;
  for (uint8_t i = 0; i < count_; ++i) {
    const uint8_t u = mapping_[i];
    Bench& b = units_[u];
    if (batch_ == Batch::Text && b.index() == targets_[i]) continue;
    const bool ok = batch_ == Batch::Text ? b.goTo(targets_[i], now) :
        batch_ == Batch::Home ? b.home(now) : b.cycle(now);
    if (!ok) { fail(u, "batch_start_failed"); return; }
    started = true;
  }
  if (!started) batch_ = Batch::None;
}

void Panel::tick(uint32_t now, uint16_t rawHighMask, uint16_t allowSteps) {
  for (uint8_t i = 0; i < count_; ++i) {
    const uint8_t u = mapping_[i];
    const bool high = (rawHighMask & (1U << u)) != 0;
    units_[u].tick(now, polarity_[u] ? !high : high, (allowSteps & (1U << u)) != 0);
    if (units_[u].error()) { fail(u, units_[u].error()); return; }
  }
  if (batching()) {
    for (uint8_t i = 0; i < count_; ++i) if (units_[mapping_[i]].busy()) return;
    batch_ = Batch::None;
  }
}
}
