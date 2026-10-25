// SPDX-License-Identifier: GPL-3.0-only
#include "bench.h"

namespace mega {
namespace {
// Floor(sqrt(value)), using integers only on the 8-bit AVR.
uint16_t integerRoot(uint32_t value) {
  uint32_t root = 0, bit = 1UL << 30;
  while (bit > value) bit >>= 2;
  while (bit) {
    if (value >= root + bit) {
      value -= root + bit;
      root = (root >> 1) + bit;
    } else root >>= 1;
    bit >>= 2;
  }
  return static_cast<uint16_t>(root);
}
}

bool Bench::configure(const Config& c) {
  if (busy() || c.revolution < 1000 || c.revolution > 20000 ||
      c.offset >= c.revolution || c.intervalUs < kMinIntervalUs ||
      c.intervalUs > kMaxIntervalUs || c.acceleration < 100 || c.acceleration > 6000 ||
      (c.direction != 1 && c.direction != -1)) return false;
  uint8_t seen = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    if (c.order[i] > 3 || (seen & (1 << c.order[i]))) return false;
    seen |= 1 << c.order[i];
  }
  stop();
  phaseKnown_ = false;
  config_ = c;
  return true;
}

bool Bench::arm() {
  if (busy()) return false;
  error_ = nullptr;
  armed_ = true;
  return true;
}

void Bench::stop() {
  mode_ = Mode::Idle;
  armed_ = false;
  reference_ = Reference::None;
  coils_ = 0;
  remaining_ = rampSteps_ = 0;
  error_ = nullptr;
}

bool Bench::setCurrent(uint8_t index) {
  if (busy() || index >= kFlapCount) return false;
  stop();  // Recording a visible flap never energizes or automatically arms.
  position_ = positionFor(config_.revolution, index);
  index_ = index;
  reference_ = Reference::Manual;
  return true;
}

void Bench::fail(const char* reason) {
  stop();
  error_ = reason;
}

bool Bench::begin(Job job, uint32_t now) {
  if (!armed_ || busy()) return false;
  job_ = job;
  error_ = nullptr;
  startedAt_ = lastStepAt_ = stageAt_ = now;
  // Start fresh stall monitoring even if the bench has been idle for a while.
  lastTickAt_ = now;
  tickKnown_ = true;
  totalSteps_ = stageSteps_ = 0;
  measuredCount_ = 0;
  coils_ = 0;
  cycleFinal_ = false;
  return true;
}

bool Bench::step(uint16_t count, uint32_t now) {
  if (count == 0 || count > 20000 || !begin(Job::Raw, now)) return false;
  reference_ = Reference::None;
  mode_ = Mode::Raw;
  startRamp(count, now);
  return true;
}

bool Bench::led(uint32_t now) {
  if (!begin(Job::Led, now)) return false;
  reference_ = Reference::None;
  ledIndex_ = 0;
  coils_ = 1;
  mode_ = Mode::Led;
  return true;
}

void Bench::startHome(uint32_t now) {
  reference_ = Reference::None;
  stageSteps_ = 0;
  lastStepAt_ = now;
  mode_ = Mode::HomeStart;
}

bool Bench::home(uint32_t now) {
  if (!begin(Job::Home, now)) return false;
  startHome(now);
  return true;
}

bool Bench::measure(uint32_t now) {
  if (!begin(Job::Measure, now)) return false;
  startHome(now);
  return true;
}

bool Bench::goTo(uint8_t target, uint32_t now) {
  if (target >= kFlapCount || !positioned() || !begin(Job::Move, now)) return false;
  target_ = target;
  if (homed() && target < index_) startHome(now);
  else startMove(target, now);
  return true;
}

bool Bench::cycle(uint32_t now) {
  if (!positioned() || !begin(Job::Cycle, now)) return false;
  if (manual()) {
    cycleStart_ = index_;
    startMove((index_ + 1) % kFlapCount, now);
  } else startHome(now);
  return true;
}

void Bench::advance(uint32_t now) {
  static const uint8_t halfSteps[8] = {1, 3, 2, 6, 4, 12, 8, 9};
  if (!phaseKnown_) { phase_ = 0; phaseKnown_ = true; }
  else phase_ = static_cast<uint8_t>((phase_ + config_.direction + 8) % 8);
  coils_ = 0;
  for (uint8_t i = 0; i < 4; ++i)
    if (halfSteps[phase_] & (1 << i)) coils_ |= 1 << config_.order[i];
  lastStepAt_ = now;  // No catch-up burst after a late tick.
  ++totalSteps_;
  ++stageSteps_;
}

void Bench::settle(uint32_t now) {
  mode_ = Mode::Settle;
  stageAt_ = now;
}

uint16_t Bench::referenceInterval() const {
  // Fast cruise settings must not speed up Hall searches or measurement.
  return config_.intervalUs > kReferenceIntervalUs ? config_.intervalUs : kReferenceIntervalUs;
}

void Bench::startRamp(uint16_t count, uint32_t now) {
  remaining_ = rampSteps_ = count;
  lastStepAt_ = now;
  updateRampInterval();
}

void Bench::updateRampInterval() {
  const uint16_t cruise = mode_ == Mode::Offset ? referenceInterval() : config_.intervalUs;
  if (cruise >= kStartIntervalUs) { rampIntervalUs_ = cruise; return; }
  // Mirror the acceleration distance about the move midpoint. Short moves form a
  // triangle; longer moves reach SPEED, cruise, then brake to the starting speed.
  const uint16_t completed = rampSteps_ - remaining_;
  const uint16_t braking = remaining_ - 1;
  const uint16_t distance = completed < braking ? completed : braking;
  const uint32_t startSpeed = 1000000UL / kStartIntervalUs;
  // v^2 = v_start^2 + 2*a*distance. The validated bounds keep this in uint32_t.
  const uint16_t speed = integerRoot(startSpeed * startSpeed +
      2UL * config_.acceleration * distance);
  const uint16_t interval = static_cast<uint16_t>((1000000UL + speed - 1) / speed);
  rampIntervalUs_ = interval > cruise ? interval : cruise;
}

void Bench::startMove(uint8_t target, uint32_t now) {
  target_ = target;
  const uint16_t destination = positionFor(config_.revolution, target);
  mode_ = Mode::Moving;
  if (destination == position_) { index_ = target; settle(now); }
  else startRamp(destination > position_ ? destination - position_ :
      config_.revolution - position_ + destination, now);
}

void Bench::homeDone(uint32_t now) {
  position_ = index_ = 0;
  reference_ = Reference::Hall;
  if (job_ == Job::Move && target_ != 0) startMove(target_, now);
  else settle(now);
}

void Bench::markerFound(uint32_t now) {
  if (job_ == Job::Measure) {
    markerStep_ = totalSteps_;
    markerCleared_ = false;
    stageSteps_ = 0;
    mode_ = Mode::Measure;
  } else if (config_.offset) {
    mode_ = Mode::Offset;
    startRamp(config_.offset, now);
  } else homeDone(now);
}

void Bench::tick(uint32_t now, bool activeHall) {
  if (!hallKnown_) {
    hallKnown_ = true;
    rawHall_ = stableHall_ = activeHall;
    hallChangedAt_ = now;
  }
  if (rawHall_ != activeHall) {
    rawHall_ = activeHall;
    hallChangedAt_ = now;
  }
  const bool hallReady = static_cast<uint32_t>(now - hallChangedAt_) >= kDebounceUs;
  if (hallReady) stableHall_ = rawHall_;
  const bool stalled = tickKnown_ && busy() &&
      static_cast<uint32_t>(now - lastTickAt_) > kStallUs;
  lastTickAt_ = now;
  tickKnown_ = true;
  if (!busy()) return;
  if (stalled) { fail("loop_stall"); return; }
  if (static_cast<uint32_t>(now - startedAt_) >= kTimeoutUs) {
    fail("motion_timeout"); return;
  }
  if (mode_ == Mode::Led) {
    if (static_cast<uint32_t>(now - stageAt_) >= 1000000) {
      stageAt_ = now;
      if (++ledIndex_ == 4) { coils_ = 0; mode_ = Mode::Idle; }
      else coils_ = 1 << ledIndex_;
    }
    return;
  }
  if (mode_ == Mode::Settle) {
    if (static_cast<uint32_t>(now - stageAt_) >= 100000) {
      coils_ = 0;
      if (job_ == Job::Cycle && !cycleFinal_) {
        mode_ = Mode::Pause;
        stageAt_ = now;
      } else mode_ = Mode::Idle;
    }
    return;
  }
  if (mode_ == Mode::Pause) {
    if (static_cast<uint32_t>(now - stageAt_) >= 400000) {
      if (manual()) startMove((index_ + 1) % kFlapCount, now);
      else if (index_ == kFlapCount - 1) { cycleFinal_ = true; startHome(now); }
      else startMove(index_ + 1, now);
    }
    return;
  }
  if (mode_ == Mode::HomeStart) {
    if (hallReady) {
      mode_ = stableHall_ ? Mode::Clear : Mode::Seek;
      stageSteps_ = 0;
      lastStepAt_ = now;
    }
    return;
  }
  if (mode_ == Mode::Clear && hallReady && !stableHall_) {
    mode_ = Mode::Seek;
    stageSteps_ = 0;
    lastStepAt_ = now;
    return;
  }
  if (mode_ == Mode::Seek && hallReady && stableHall_) {
    markerFound(now);
    return;
  }
  if (mode_ == Mode::Measure && hallReady) {
    if (!stableHall_) markerCleared_ = true;
    if (stableHall_ && markerCleared_) {
      measured_[measuredCount_++] = static_cast<uint16_t>(totalSteps_ - markerStep_);
      markerStep_ = totalSteps_;
      stageSteps_ = 0;
      markerCleared_ = false;
      if (measuredCount_ == 3) { settle(now); return; }
    }
  }
  if ((mode_ == Mode::Clear || mode_ == Mode::Seek || mode_ == Mode::Measure) &&
      stageSteps_ >= static_cast<uint32_t>(config_.revolution) * 3 / 2) {
    fail(mode_ == Mode::Clear ? "hall_did_not_release" : "hall_edge_missing");
    return;
  }
  const bool ramped = mode_ == Mode::Raw || mode_ == Mode::Offset || mode_ == Mode::Moving;
  const uint16_t interval = ramped ? rampIntervalUs_ : referenceInterval();
  if (static_cast<uint32_t>(now - lastStepAt_) < interval) return;
  advance(now);
  if (ramped) {
    if (mode_ == Mode::Moving && ++position_ == config_.revolution) position_ = 0;
    if (--remaining_ == 0) {
      if (mode_ == Mode::Offset) homeDone(now);
      else {
        if (mode_ == Mode::Moving) {
          index_ = target_;
          if (job_ == Job::Cycle && manual() && index_ == cycleStart_) cycleFinal_ = true;
        }
        settle(now);
      }
    } else updateRampInterval();
  }
}
}
