// SPDX-License-Identifier: GPL-3.0-only
#include "motion.h"

#include <stdint.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

using splitflap::FaultCode;
using splitflap::MotionController;
using splitflap::State;
using splitflap::UnitConfig;
using splitflap::kAllUnitsMask;
using splitflap::kFlapCount;
using splitflap::kHallDebounceUs;
using splitflap::kSettleUs;
using splitflap::kUnitCount;

#define CHECK(expression) do { if (!(expression)) { \
  throw std::runtime_error(std::string(__FILE__) + ":" + \
      std::to_string(__LINE__) + ": " #expression); \
} } while (0)

namespace {

UnitConfig enabledConfig(uint32_t revolution = 1000, uint32_t offset = 0,
                         uint32_t interval = 3000, int8_t direction = 1) {
  UnitConfig config;
  config.enabled = true;
  config.stepsPerRevolution = revolution;
  config.homeOffsetSteps = offset;
  config.halfStepIntervalUs = interval;
  config.phaseDirection = direction;
  return config;
}

struct Rig {
  MotionController controller;
  uint32_t now;
  uint16_t hall;
  uint32_t phases[kUnitCount];
  uint8_t previous[kUnitCount];

  explicit Rig(uint32_t start = 0) : now(start), hall(0), phases{}, previous{} {}

  void tick(uint32_t advance = 0) {
    now += advance;
    controller.tick(now, hall);
    for (uint8_t i = 0; i < kUnitCount; ++i) {
      const uint8_t mask = controller.coilMask(i);
      CHECK(mask <= 15);
      if (mask != 0 && mask != previous[i]) ++phases[i];
      previous[i] = mask;
    }
  }

  void setHall(uint8_t unit, bool active) {
    const uint16_t bit = static_cast<uint16_t>(1u << unit);
    hall = active ? static_cast<uint16_t>(hall | bit) :
                    static_cast<uint16_t>(hall & ~bit);
  }

  void makeReady(uint8_t unit = 0, uint32_t offset = 0) {
    CHECK(controller.configure(unit, enabledConfig(1000, offset)));
    CHECK(controller.beginHome(static_cast<uint16_t>(1u << unit)));
    setHall(unit, false);
    tick();
    CHECK(controller.state(unit) == State::ClearMarker);
    tick(kHallDebounceUs);
    CHECK(controller.state(unit) == State::SeekMarker);
    tick(3000);
    setHall(unit, true);
    tick(3000);
    tick(kHallDebounceUs);
    for (uint32_t i = 0; i < offset; ++i) tick(3000);
    CHECK(controller.state(unit) == State::Settling);
    tick(kSettleUs);
    CHECK(controller.state(unit) == State::Ready);
    CHECK(controller.homed(unit));
    CHECK(controller.coilMask(unit) == 0);
    CHECK(controller.currentIndex(unit) == 0);
    CHECK(controller.stepsFromZero(unit) == 0);
  }

  void finishMove(uint8_t unit = 0) {
    tick();
    CHECK(controller.state(unit) == State::Moving);
    unsigned int guard = 0;
    while (controller.state(unit) == State::Moving && ++guard < 21000)
      tick(controller.config(unit).halfStepIntervalUs);
    CHECK(controller.state(unit) == State::Settling);
    tick(kSettleUs);
    CHECK(controller.state(unit) == State::Ready);
  }
};

void defaults_and_invalid_inputs() {
  Rig rig;
  CHECK(!rig.controller.busy());
  CHECK(rig.controller.maxConcurrent() == 1);
  for (uint8_t unit = 0; unit < kUnitCount; ++unit) {
    CHECK(!rig.controller.config(unit).enabled);
    CHECK(rig.controller.config(unit).stepsPerRevolution == 4096);
    CHECK(rig.controller.config(unit).halfStepIntervalUs == 8000);
    CHECK(!rig.controller.homed(unit));
    CHECK(rig.controller.state(unit) == State::Unhomed);
    CHECK(rig.controller.coilMask(unit) == 0);
    CHECK(!rig.controller.setTarget(unit, 1));
  }
  rig.hall = kAllUnitsMask;
  rig.tick(1000000);
  CHECK(!rig.controller.busy());
  CHECK(!rig.controller.beginHome(kAllUnitsMask));
  CHECK(!rig.controller.beginHome(0));
  CHECK(!rig.controller.beginHome(1u << kUnitCount));
  CHECK(!rig.controller.setTarget(kUnitCount, 0));
  CHECK(!rig.controller.setEnabled(kUnitCount, true));
  CHECK(!rig.controller.configure(kUnitCount, enabledConfig()));
  CHECK(!rig.controller.setMaxConcurrent(0));
  CHECK(!rig.controller.setMaxConcurrent(11));
  CHECK(rig.controller.coilMask(255) == 0);
  CHECK(rig.controller.fault(255) == FaultCode::InvalidUnit);
  CHECK(rig.controller.state(255) == State::Fault);
  CHECK(!rig.controller.config(255).enabled);
  CHECK(!rig.controller.homed(255));
  CHECK(!rig.controller.unitBusy(255));
  CHECK(rig.controller.currentIndex(255) == 0);
  CHECK(rig.controller.targetIndex(255) == 0);
  CHECK(rig.controller.stepsFromZero(255) == 0);
}

void configuration_validation_and_idle_gate() {
  Rig rig;
  UnitConfig config = enabledConfig();
  CHECK(MotionController::validConfig(config));
  const uint32_t invalidRevolutions[] = {0, 999, 20001, UINT32_MAX};
  for (uint32_t invalid : invalidRevolutions) {
    config = enabledConfig(); config.stepsPerRevolution = invalid;
    CHECK(!rig.controller.configure(0, config));
  }
  const uint32_t invalidIntervals[] = {0, 2999, 30001, UINT32_MAX};
  for (uint32_t invalid : invalidIntervals) {
    config = enabledConfig(); config.halfStepIntervalUs = invalid;
    CHECK(!rig.controller.configure(0, config));
  }
  config = enabledConfig(); config.homeOffsetSteps = 1000;
  CHECK(!rig.controller.configure(0, config));
  config.homeOffsetSteps = UINT32_MAX;
  CHECK(!rig.controller.configure(0, config));
  config = enabledConfig(); config.phaseDirection = 0;
  CHECK(!rig.controller.configure(0, config));
  config.phaseDirection = 2;
  CHECK(!rig.controller.configure(0, config));
  CHECK(rig.controller.configure(0, enabledConfig(20000, 19999, 30000, -1)));
  CHECK(rig.controller.beginHome(1));
  CHECK(!rig.controller.configure(1, enabledConfig()));
  CHECK(!rig.controller.setMaxConcurrent(2));
  CHECK(!rig.controller.setEnabled(1, true));
  CHECK(!rig.controller.config(1).enabled);
  rig.controller.stop();
  CHECK(rig.controller.configure(0, enabledConfig()));
}

void home_requests_are_atomic() {
  Rig rig;
  CHECK(rig.controller.configure(0, enabledConfig()));
  CHECK(!rig.controller.beginHome(3));
  CHECK(rig.controller.state(0) == State::Unhomed);
  CHECK(rig.controller.configure(1, enabledConfig()));
  CHECK(rig.controller.beginHome(1));
  CHECK(!rig.controller.beginHome(3));
  CHECK(rig.controller.state(1) == State::Unhomed);
  CHECK(rig.controller.beginHome(2));
  CHECK(rig.controller.state(1) == State::QueuedHome);
  CHECK(!rig.controller.beginHome(0x8001));
}

void starts_active_requires_clear_and_reentry() {
  Rig rig;
  CHECK(rig.controller.configure(0, enabledConfig(1000, 3)));
  rig.hall = 1;
  CHECK(rig.controller.beginHome(1));
  rig.tick();
  rig.tick(3000);
  CHECK(rig.controller.coilMask(0) == 1);
  CHECK(rig.controller.state(0) == State::ClearMarker);
  rig.setHall(0, false);
  rig.tick(3000);
  const uint32_t beforeClear = rig.phases[0];
  rig.tick(kHallDebounceUs - 1);
  CHECK(rig.controller.state(0) == State::ClearMarker);
  CHECK(rig.phases[0] == beforeClear);
  rig.tick(1);
  CHECK(rig.controller.state(0) == State::SeekMarker);
  rig.tick(3000);
  const uint32_t beforeMarker = rig.phases[0];
  rig.setHall(0, true);
  rig.tick(3000);
  rig.tick(kHallDebounceUs - 1);
  CHECK(!rig.controller.homed(0));
  CHECK(rig.phases[0] == beforeMarker);
  rig.tick(1);
  CHECK(rig.controller.state(0) == State::Offset);
  CHECK(rig.phases[0] == beforeMarker);
  for (int i = 0; i < 3; ++i) rig.tick(3000);
  CHECK(rig.phases[0] == beforeMarker + 3);
  CHECK(rig.controller.homed(0));
  CHECK(rig.controller.state(0) == State::Settling);
  CHECK(rig.controller.coilMask(0) != 0);
  rig.tick(kSettleUs - 1);
  CHECK(rig.controller.coilMask(0) != 0);
  rig.tick(1);
  CHECK(rig.controller.state(0) == State::Ready);
  CHECK(rig.controller.coilMask(0) == 0);
}

void starts_inactive_and_zero_offset() {
  Rig rig;
  rig.makeReady();
  CHECK(rig.phases[0] == 1);
  CHECK(!rig.controller.busy());
  CHECK(!rig.controller.setTarget(0, 45));
  CHECK(!rig.controller.setTarget(0, 255));
  CHECK(rig.controller.setTarget(0, 0));
  rig.tick(1000000);
  CHECK(!rig.controller.busy());
  CHECK(rig.phases[0] == 1);
  CHECK(rig.controller.coilMask(0) == 0);
}

void hall_bounce_does_not_home_or_move_reference() {
  Rig rig;
  CHECK(rig.controller.configure(0, enabledConfig()));
  CHECK(rig.controller.beginHome(1));
  rig.tick();
  rig.tick(kHallDebounceUs);
  rig.tick(3000);
  const uint32_t before = rig.phases[0];
  for (int bounce = 0; bounce < 5; ++bounce) {
    rig.setHall(0, true);
    rig.tick(1000);
    rig.tick(kHallDebounceUs - 1);
    CHECK(rig.controller.state(0) == State::SeekMarker);
    rig.setHall(0, false);
    rig.tick(1);
    CHECK(!rig.controller.homed(0));
  }
  CHECK(rig.phases[0] == before + 5);
  const uint32_t beforeStable = rig.phases[0];
  rig.setHall(0, true);
  rig.tick(1000);
  rig.tick(kHallDebounceUs);
  CHECK(rig.controller.state(0) == State::Settling);
  CHECK(rig.phases[0] == beforeStable);
}

void initial_inactive_must_be_stable() {
  Rig rig;
  CHECK(rig.controller.configure(0, enabledConfig()));
  CHECK(rig.controller.beginHome(1));
  rig.tick();
  rig.tick(kHallDebounceUs - 1);
  CHECK(rig.controller.state(0) == State::ClearMarker);
  rig.setHall(0, true);
  rig.tick(1);
  rig.tick(kHallDebounceUs);
  CHECK(rig.controller.state(0) == State::ClearMarker);
  CHECK(!rig.controller.homed(0));
}

void stuck_hall_both_levels_fault_and_release() {
  for (int active = 0; active <= 1; ++active) {
    Rig rig;
    CHECK(rig.controller.configure(0, enabledConfig()));
    rig.hall = static_cast<uint16_t>(active);
    CHECK(rig.controller.beginHome(1));
    rig.tick();
    for (int tick = 0; tick < 2010 && rig.controller.busy(); ++tick)
      rig.tick(3000);
    CHECK(rig.controller.state(0) == State::Fault);
    CHECK(rig.controller.fault(0) == FaultCode::HomeStepLimit);
    CHECK(rig.phases[0] == 2000);
    CHECK(!rig.controller.homed(0));
    CHECK(!rig.controller.busy());
    CHECK(rig.controller.coilMask(0) == 0);
    CHECK(!rig.controller.setTarget(0, 1));
    CHECK(rig.controller.beginHome(1));
    CHECK(rig.controller.fault(0) == FaultCode::None);
  }
}

void timeout_bounds_and_sparse_ticks() {
  Rig rig;
  CHECK(rig.controller.configure(0, enabledConfig(20000, 0, 30000)));
  CHECK(rig.controller.beginHome(1));
  rig.hall = 1;
  rig.tick();
  rig.tick(119999999);
  CHECK(rig.controller.state(0) == State::ClearMarker);
  CHECK(rig.phases[0] == 1);  // No catch-up after a long scheduling delay.
  rig.tick(1);
  CHECK(rig.controller.state(0) == State::Fault);
  CHECK(rig.controller.fault(0) == FaultCode::HomeTimeout);
  CHECK(rig.controller.coilMask(0) == 0);

  Rig shortRig;
  CHECK(shortRig.controller.configure(0, enabledConfig()));
  CHECK(shortRig.controller.beginHome(1));
  shortRig.tick();
  shortRig.tick(59999999);
  CHECK(shortRig.controller.state(0) == State::SeekMarker);
  shortRig.tick(1);
  CHECK(shortRig.controller.fault(0) == FaultCode::HomeTimeout);

  Rig defaultRig;
  UnitConfig config;
  config.enabled = true;
  CHECK(defaultRig.controller.configure(0, config));
  CHECK(defaultRig.controller.beginHome(1));
  defaultRig.tick();
  const uint32_t expected = 2 * 4096 * 8000 + 5000000;
  defaultRig.tick(expected - 1);
  CHECK(defaultRig.controller.state(0) == State::SeekMarker);
  defaultRig.tick(1);
  CHECK(defaultRig.controller.fault(0) == FaultCode::HomeTimeout);
}

void offset_is_in_total_homing_step_budget() {
  Rig rig;
  CHECK(rig.controller.configure(0, enabledConfig(1000, 100)));
  CHECK(rig.controller.beginHome(1));
  rig.tick();
  rig.tick(kHallDebounceUs);
  for (int i = 0; i < 1950; ++i) rig.tick(3000);
  rig.hall = 1;
  rig.tick();
  rig.tick(kHallDebounceUs);
  CHECK(rig.controller.state(0) == State::Offset);
  for (int i = 0; i < 51; ++i) rig.tick(3000);
  CHECK(rig.controller.fault(0) == FaultCode::HomeStepLimit);
  CHECK(rig.phases[0] == 2000);
  CHECK(!rig.controller.homed(0));
  CHECK(rig.controller.coilMask(0) == 0);
}

void halfstep_sequence_and_direction() {
  const uint8_t forward[] = {1, 3, 2, 6, 4, 12, 8, 9, 1};
  const uint8_t reverse[] = {1, 9, 8, 12, 4, 6, 2, 3, 1};
  for (int direction : {-1, 1}) {
    Rig rig;
    CHECK(rig.controller.configure(0, enabledConfig(1000, 0, 3000,
                                                   static_cast<int8_t>(direction))));
    rig.hall = 1;
    CHECK(rig.controller.beginHome(1));
    rig.tick();
    for (size_t i = 0; i < sizeof(forward); ++i) {
      rig.tick(2999);
      CHECK(rig.phases[0] == i);
      rig.tick(1);
      CHECK(rig.controller.coilMask(0) == (direction == 1 ? forward[i] : reverse[i]));
      CHECK(rig.phases[0] == i + 1);
      rig.tick();
      CHECK(rig.phases[0] == i + 1);
    }
  }
}

void delayed_tick_never_catches_up() {
  Rig rig;
  rig.makeReady();
  CHECK(rig.controller.setTarget(0, 10));
  rig.tick();
  const uint32_t before = rig.phases[0];
  rig.tick(10000000);
  CHECK(rig.phases[0] == before + 1);
  CHECK(rig.controller.stepsFromZero(0) == 1);
  for (int i = 0; i < 10; ++i) rig.tick();
  CHECK(rig.phases[0] == before + 1);
  rig.tick(2999);
  CHECK(rig.phases[0] == before + 1);
  rig.tick(1);
  CHECK(rig.phases[0] == before + 2);
}

void absolute_positions_without_division_drift() {
  Rig rig;
  rig.makeReady();
  const uint32_t before = rig.phases[0];
  for (uint8_t target = 1; target < kFlapCount; ++target) {
    CHECK(rig.controller.setTarget(0, target));
    rig.finishMove();
    const uint32_t expected = (1000u * target + 22u) / 45u;
    CHECK(rig.controller.stepsFromZero(0) == expected);
    CHECK(rig.controller.currentIndex(0) == target);
    CHECK(rig.phases[0] - before == expected);
    CHECK(rig.controller.coilMask(0) == 0);
  }
  CHECK(MotionController::stepsForIndex(4096, 44) == 4005);
  CHECK(MotionController::stepsForIndex(4096, 45) == 4096);
  CHECK(MotionController::stepsForIndex(4076, 45) == 4076);
}

void reversed_phases_keep_forward_logical_positions() {
  Rig rig;
  rig.makeReady();
  UnitConfig config = rig.controller.config(0);
  config.phaseDirection = -1;
  CHECK(rig.controller.configure(0, config));
  CHECK(!rig.controller.homed(0));
  rig.hall = 0;
  CHECK(rig.controller.beginHome(1));
  rig.tick(); rig.tick(kHallDebounceUs); rig.tick(3000);
  rig.hall = 1;
  rig.tick(); rig.tick(kHallDebounceUs); rig.tick(kSettleUs);
  CHECK(rig.controller.setTarget(0, 3));
  rig.finishMove();
  CHECK(rig.controller.currentIndex(0) == 3);
  CHECK(rig.controller.stepsFromZero(0) == 67);
}

void wrap_rehomes_before_target() {
  for (uint8_t target : {uint8_t(0), uint8_t(2)}) {
    Rig rig;
    rig.makeReady(0, 2);
    CHECK(rig.controller.setTarget(0, 44));
    rig.finishMove();
    CHECK(rig.controller.setTarget(0, target));
    CHECK(rig.controller.state(0) == State::QueuedHome);
    CHECK(!rig.controller.homed(0));
    CHECK(rig.controller.targetIndex(0) == target);
    rig.setHall(0, false);
    rig.tick(); rig.tick(kHallDebounceUs); rig.tick(3000);
    CHECK(rig.controller.state(0) == State::SeekMarker);
    rig.setHall(0, true);
    rig.tick(); rig.tick(kHallDebounceUs);
    CHECK(rig.controller.state(0) == State::Offset);
    rig.tick(3000); rig.tick(3000);
    CHECK(rig.controller.homed(0));
    CHECK(rig.controller.stepsFromZero(0) == 0);
    if (target != 0) {
      CHECK(rig.controller.state(0) == State::Moving);
      rig.finishMove();
    } else {
      CHECK(rig.controller.state(0) == State::Settling);
      rig.tick(kSettleUs);
    }
    CHECK(rig.controller.state(0) == State::Ready);
    CHECK(rig.controller.currentIndex(0) == target);
    CHECK(rig.controller.stepsFromZero(0) == MotionController::stepsForIndex(1000, target));
    CHECK(rig.controller.coilMask(0) == 0);
  }
}

void concurrency_covers_home_move_and_settling() {
  for (uint8_t limit : {uint8_t(1), uint8_t(2), uint8_t(10)}) {
    Rig rig;
    CHECK(rig.controller.setMaxConcurrent(limit));
    for (uint8_t unit = 0; unit < kUnitCount; ++unit)
      CHECK(rig.controller.configure(unit, enabledConfig()));
    CHECK(rig.controller.beginHome(kAllUnitsMask));
    uint32_t stateTicks[kUnitCount] = {};
    for (int cycle = 0; cycle < 2000 && rig.controller.busy(); ++cycle) {
      for (uint8_t unit = 0; unit < kUnitCount; ++unit) {
        if (rig.controller.state(unit) == State::SeekMarker) {
          ++stateTicks[unit];
          rig.setHall(unit, stateTicks[unit] >= 4);
        } else if (rig.controller.state(unit) == State::QueuedHome) {
          rig.setHall(unit, false);
        }
      }
      rig.tick(3000);
      unsigned int slots = 0, powered = 0;
      for (uint8_t unit = 0; unit < kUnitCount; ++unit) {
        const State state = rig.controller.state(unit);
        if (state == State::ClearMarker || state == State::SeekMarker ||
            state == State::Offset || state == State::Moving || state == State::Settling)
          ++slots;
        if (rig.controller.coilMask(unit) != 0) ++powered;
      }
      CHECK(slots <= limit);
      CHECK(powered <= limit);
    }
    CHECK(!rig.controller.busy());
    for (uint8_t unit = 0; unit < kUnitCount; ++unit) {
      CHECK(rig.controller.homed(unit));
      CHECK(rig.controller.state(unit) == State::Ready);
      CHECK(rig.controller.setTarget(unit, static_cast<uint8_t>(unit + 1)));
    }
    for (int cycle = 0; cycle < 3000 && rig.controller.busy(); ++cycle) {
      rig.tick(3000);
      unsigned int slots = 0, powered = 0;
      for (uint8_t unit = 0; unit < kUnitCount; ++unit) {
        if (rig.controller.state(unit) == State::Moving ||
            rig.controller.state(unit) == State::Settling) ++slots;
        if (rig.controller.coilMask(unit) != 0) ++powered;
      }
      CHECK(slots <= limit);
      CHECK(powered <= limit);
    }
    CHECK(!rig.controller.busy());
    for (uint8_t unit = 0; unit < kUnitCount; ++unit) {
      CHECK(rig.controller.currentIndex(unit) == unit + 1);
      CHECK(rig.controller.coilMask(unit) == 0);
    }
  }
}

void fault_releases_slot_for_queued_unit() {
  Rig rig;
  CHECK(rig.controller.configure(0, enabledConfig()));
  CHECK(rig.controller.configure(1, enabledConfig()));
  CHECK(rig.controller.beginHome(3));
  rig.hall = 1;
  rig.tick();
  CHECK(rig.controller.state(1) == State::QueuedHome);
  for (int i = 0; i < 2001; ++i) rig.tick(3000);
  CHECK(rig.controller.state(0) == State::Fault);
  CHECK(rig.controller.coilMask(0) == 0);
  CHECK(rig.controller.state(1) == State::ClearMarker);
}

void stop_fault_disable_and_reconfigure_invalidate_position() {
  Rig rig;
  rig.makeReady();
  CHECK(rig.controller.setTarget(0, 10));
  rig.tick(); rig.tick(3000);
  CHECK(rig.controller.coilMask(0) != 0);
  CHECK(!rig.controller.setTarget(0, 2));
  CHECK(!rig.controller.beginHome(1));
  rig.controller.stop();
  CHECK(!rig.controller.busy());
  CHECK(!rig.controller.homed(0));
  CHECK(rig.controller.coilMask(0) == 0);
  CHECK(rig.controller.state(0) == State::Unhomed);
  CHECK(!rig.controller.setTarget(0, 1));
  CHECK(rig.controller.beginHome(1));
  rig.tick(); rig.tick(3000);
  rig.controller.faultAll(FaultCode::BusError);
  CHECK(rig.controller.state(0) == State::Fault);
  CHECK(rig.controller.fault(0) == FaultCode::BusError);
  CHECK(rig.controller.coilMask(0) == 0);
  CHECK(!rig.controller.homed(0));
  CHECK(!rig.controller.busy());
  CHECK(rig.controller.state(1) == State::Unhomed);
  CHECK(rig.controller.setEnabled(0, false));
  CHECK(!rig.controller.beginHome(1));
  CHECK(rig.controller.state(0) == State::Unhomed);
  CHECK(rig.controller.setEnabled(0, true));
  CHECK(!rig.controller.homed(0));
  CHECK(!rig.controller.busy());

  Rig ready;
  ready.makeReady();
  CHECK(ready.controller.configure(0, ready.controller.config(0)));
  CHECK(!ready.controller.homed(0));
  CHECK(!ready.controller.setTarget(0, 1));

  Rig queued;
  CHECK(queued.controller.configure(0, enabledConfig()));
  CHECK(queued.controller.configure(1, enabledConfig()));
  CHECK(queued.controller.beginHome(3));
  queued.tick();
  CHECK(queued.controller.setEnabled(0, false));
  CHECK(queued.controller.coilMask(0) == 0);
  CHECK(!queued.controller.homed(0));
  queued.tick();
  CHECK(queued.controller.state(1) == State::ClearMarker);
  queued.controller.stop(2);
  CHECK(!queued.controller.busy());
}

void micros_rollover_during_debounce_steps_and_settle() {
  Rig rig(UINT32_MAX - 2000);
  rig.makeReady();
  CHECK(rig.now < 200000);
  CHECK(rig.controller.setTarget(0, 1));
  rig.finishMove();
  CHECK(rig.controller.stepsFromZero(0) == 22);

  Rig stepping(UINT32_MAX - 7000);
  CHECK(stepping.controller.configure(0, enabledConfig()));
  stepping.hall = 1;
  CHECK(stepping.controller.beginHome(1));
  stepping.tick();
  stepping.tick(3000); stepping.tick(3000); stepping.tick(3000);
  CHECK(stepping.phases[0] == 3);
  CHECK(stepping.controller.coilMask(0) == 2);

  Rig settling(UINT32_MAX - 20000);
  settling.makeReady();
  CHECK(settling.controller.state(0) == State::Ready);
  CHECK(settling.controller.coilMask(0) == 0);

  Rig timeout(UINT32_MAX - 10);
  CHECK(timeout.controller.configure(0, enabledConfig()));
  CHECK(timeout.controller.beginHome(1));
  timeout.tick(); timeout.tick(60000000);
  CHECK(timeout.controller.fault(0) == FaultCode::HomeTimeout);
}

void labels_cover_states_and_faults() {
  CHECK(std::strcmp(MotionController::stateName(State::Unhomed), "unhomed") == 0);
  CHECK(std::strcmp(MotionController::stateName(State::QueuedHome), "queued_home") == 0);
  CHECK(std::strcmp(MotionController::stateName(State::ClearMarker), "clear_marker") == 0);
  CHECK(std::strcmp(MotionController::stateName(State::SeekMarker), "seek_marker") == 0);
  CHECK(std::strcmp(MotionController::stateName(State::Offset), "offset") == 0);
  CHECK(std::strcmp(MotionController::stateName(State::Ready), "ready") == 0);
  CHECK(std::strcmp(MotionController::stateName(State::Moving), "moving") == 0);
  CHECK(std::strcmp(MotionController::stateName(State::Settling), "settling") == 0);
  CHECK(std::strcmp(MotionController::stateName(State::Fault), "fault") == 0);
  CHECK(std::strcmp(MotionController::faultName(FaultCode::None), "none") == 0);
  CHECK(std::strcmp(MotionController::faultName(FaultCode::HomeStepLimit), "home_step_limit") == 0);
  CHECK(std::strcmp(MotionController::faultName(FaultCode::HomeTimeout), "home_timeout") == 0);
  CHECK(std::strcmp(MotionController::faultName(FaultCode::OutputFault), "output_fault") == 0);
  CHECK(std::strcmp(MotionController::faultName(FaultCode::BusError), "bus_error") == 0);
  CHECK(std::strcmp(MotionController::faultName(FaultCode::InvalidUnit), "invalid_unit") == 0);
}

}  // namespace

int main() {
  struct Test { const char* name; void (*run)(); };
  const Test tests[] = {
    {"defaults and invalid inputs", defaults_and_invalid_inputs},
    {"configuration validation and idle gate", configuration_validation_and_idle_gate},
    {"atomic home requests", home_requests_are_atomic},
    {"active marker clearing and reentry", starts_active_requires_clear_and_reentry},
    {"inactive marker and zero offset", starts_inactive_and_zero_offset},
    {"Hall bounce debounce", hall_bounce_does_not_home_or_move_reference},
    {"initial inactive stability", initial_inactive_must_be_stable},
    {"stuck Hall levels", stuck_hall_both_levels_fault_and_release},
    {"timeout bounds and sparse ticks", timeout_bounds_and_sparse_ticks},
    {"total homing step budget", offset_is_in_total_homing_step_budget},
    {"half-step sequence and reversal", halfstep_sequence_and_direction},
    {"no delayed catch-up", delayed_tick_never_catches_up},
    {"absolute position rounding", absolute_positions_without_division_drift},
    {"reverse phase forward positions", reversed_phases_keep_forward_logical_positions},
    {"wrap rehome and pending target", wrap_rehomes_before_target},
    {"bounded scheduling", concurrency_covers_home_move_and_settling},
    {"fault frees scheduler slot", fault_releases_slot_for_queued_unit},
    {"stop fault disable reconfiguration", stop_fault_disable_and_reconfigure_invalidate_position},
    {"32-bit micros rollover", micros_rollover_during_debounce_steps_and_settle},
    {"state and fault labels", labels_cover_states_and_faults},
  };
  unsigned int passed = 0;
  for (const Test& test : tests) {
    try {
      test.run();
      std::cout << "PASS " << test.name << '\n';
      ++passed;
    } catch (const std::exception& error) {
      std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
      return EXIT_FAILURE;
    }
  }
  std::cout << passed << " native motion tests passed\n";
  return EXIT_SUCCESS;
}
