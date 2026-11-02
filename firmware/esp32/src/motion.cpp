// SPDX-License-Identifier: GPL-3.0-only
#include "motion.h"

namespace splitflap {
namespace {
const uint8_t kHalfStepMasks[8] = {1, 3, 2, 6, 4, 12, 8, 9};
const uint32_t kMinimumHomeTimeoutUs = 60000000;
const uint32_t kMaximumHomeTimeoutUs = 120000000;
const uint32_t kHomeTimeoutMarginUs = 5000000;
}

MotionController::Unit::Unit()
    : state(State::Unhomed), fault(FaultCode::None), positionValid(false),
      pendingTarget(false), phaseInitialized(false), rawHall(false), current(0),
      target(0), phase(0), coils(0), position(0), remaining(0), homeSteps(0),
      homeStartedUs(0), homeTimeoutUs(0), lastStepUs(0), hallChangedUs(0),
      settlingStartedUs(0) {}

MotionController::MotionController() : maxConcurrent_(1), nextScheduled_(0) {}

bool MotionController::validConfig(const UnitConfig& config) {
  return config.stepsPerRevolution >= 1000 &&
         config.stepsPerRevolution <= 20000 &&
         config.homeOffsetSteps < config.stepsPerRevolution &&
         config.halfStepIntervalUs >= 3000 &&
         config.halfStepIntervalUs <= 30000 &&
         (config.phaseDirection == 1 || config.phaseDirection == -1);
}

bool MotionController::configure(uint8_t unit, const UnitConfig& config) {
  if (unit >= kUnitCount || !validConfig(config) || busy()) return false;
  units_[unit].config = config;
  invalidate(units_[unit], State::Unhomed, FaultCode::None);
  return true;
}

const UnitConfig& MotionController::config(uint8_t unit) const {
  // The fallback is disabled and cannot accidentally enable an invalid unit.
  static const UnitConfig invalid;
  return unit < kUnitCount ? units_[unit].config : invalid;
}

bool MotionController::setMaxConcurrent(uint8_t count) {
  if (count < 1 || count > kUnitCount || busy()) return false;
  maxConcurrent_ = count;
  return true;
}

uint8_t MotionController::maxConcurrent() const { return maxConcurrent_; }

bool MotionController::setEnabled(uint8_t unit, bool enabled) {
  if (unit >= kUnitCount || (enabled && busy())) return false;
  units_[unit].config.enabled = enabled;
  invalidate(units_[unit], State::Unhomed, FaultCode::None);
  return true;
}

bool MotionController::beginHome(uint16_t mask) {
  if (mask == 0 || (mask & ~kAllUnitsMask) != 0) return false;
  for (uint8_t i = 0; i < kUnitCount; ++i) {
    if ((mask & (1u << i)) && (!units_[i].config.enabled || unitBusy(i)))
      return false;
  }
  for (uint8_t i = 0; i < kUnitCount; ++i) {
    if (mask & (1u << i))
      invalidate(units_[i], State::QueuedHome, FaultCode::None);
  }
  return true;
}

bool MotionController::setTarget(uint8_t unit, uint8_t index) {
  if (unit >= kUnitCount || index >= kFlapCount) return false;
  Unit& item = units_[unit];
  if (!item.config.enabled || !item.positionValid ||
      item.state != State::Ready || item.pendingTarget)
    return false;
  item.target = index;
  if (index == item.current) return true;
  item.pendingTarget = true;
  if (index < item.current) {
    // Do not trust a position that requires crossing the marker. Retain only
    // the requested target while the unit waits for a homing slot.
    item.positionValid = false;
    item.state = State::QueuedHome;
  }
  return true;
}

bool MotionController::ownsSlot(const Unit& unit) {
  return isHoming(unit.state) || unit.state == State::Moving ||
         unit.state == State::Settling;
}

bool MotionController::isHoming(State state) {
  return state == State::ClearMarker || state == State::SeekMarker ||
         state == State::Offset;
}

bool MotionController::elapsed(uint32_t now, uint32_t since,
                               uint32_t duration) {
  return static_cast<uint32_t>(now - since) >= duration;
}

void MotionController::invalidate(Unit& unit, State state, FaultCode fault) {
  unit.state = state;
  unit.fault = fault;
  unit.positionValid = false;
  unit.pendingTarget = false;
  unit.coils = 0;
  unit.current = 0;
  unit.target = 0;
  unit.position = 0;
  unit.remaining = 0;
  unit.homeSteps = 0;
  // Preserve the last electrical phase across releases. Rehoming establishes
  // mechanical position; a stop never implies the drum remains where it was.
}

void MotionController::stop(uint16_t mask) {
  for (uint8_t i = 0; i < kUnitCount; ++i) {
    if (mask & (1u << i))
      invalidate(units_[i], State::Unhomed, FaultCode::None);
  }
}

void MotionController::faultAll(FaultCode code) {
  if (code == FaultCode::None) code = FaultCode::OutputFault;
  for (uint8_t i = 0; i < kUnitCount; ++i) {
    invalidate(units_[i], units_[i].config.enabled ? State::Fault : State::Unhomed,
               units_[i].config.enabled ? code : FaultCode::None);
  }
}

void MotionController::startHome(Unit& unit, uint32_t nowUs, bool activeHall) {
  unit.state = State::ClearMarker;
  unit.fault = FaultCode::None;
  unit.positionValid = false;
  unit.homeSteps = 0;
  unit.homeStartedUs = nowUs;
  unit.lastStepUs = nowUs;
  unit.rawHall = activeHall;
  unit.hallChangedUs = nowUs;
  // The step and wall-clock bounds are independent. Very slow / large-drum
  // configurations may hit the two-minute safety cap before two revolutions.
  const uint32_t expected = 2u * unit.config.stepsPerRevolution *
                            unit.config.halfStepIntervalUs + kHomeTimeoutMarginUs;
  unit.homeTimeoutUs = expected < kMinimumHomeTimeoutUs ? kMinimumHomeTimeoutUs :
                      expected > kMaximumHomeTimeoutUs ? kMaximumHomeTimeoutUs :
                      expected;
}

void MotionController::startMove(Unit& unit, uint32_t nowUs) {
  const uint32_t destination = stepsForIndex(unit.config.stepsPerRevolution,
                                              unit.target);
  unit.remaining = destination - unit.position;
  unit.pendingTarget = false;
  unit.lastStepUs = nowUs;
  unit.state = State::Moving;
  if (unit.remaining == 0) settle(unit, nowUs);
}

void MotionController::advancePhase(Unit& unit, uint32_t nowUs) {
  if (!unit.phaseInitialized) {
    unit.phase = 0;
    unit.phaseInitialized = true;
  } else {
    unit.phase = static_cast<uint8_t>((unit.phase +
                        (unit.config.phaseDirection > 0 ? 1 : 7)) & 7);
  }
  unit.coils = kHalfStepMasks[unit.phase];
  // Advance from now, never from a previous deadline: delayed ticks do not
  // generate catch-up bursts, and a tick emits at most one phase per unit.
  unit.lastStepUs = nowUs;
}

void MotionController::settle(Unit& unit, uint32_t nowUs) {
  unit.current = unit.target;
  unit.state = State::Settling;
  unit.settlingStartedUs = nowUs;
}

void MotionController::finishHome(Unit& unit, uint32_t nowUs) {
  unit.position = 0;
  unit.current = 0;
  unit.positionValid = true;
  if (unit.pendingTarget && unit.target != 0) {
    startMove(unit, nowUs);
  } else {
    unit.pendingTarget = false;
    unit.target = 0;
    settle(unit, nowUs);
  }
}

void MotionController::runUnit(Unit& unit, uint32_t nowUs, bool activeHall) {
  if (unit.state == State::Settling) {
    if (elapsed(nowUs, unit.settlingStartedUs, kSettleUs)) {
      unit.coils = 0;
      unit.state = State::Ready;
    }
    return;
  }
  if (isHoming(unit.state)) {
    if (elapsed(nowUs, unit.homeStartedUs, unit.homeTimeoutUs)) {
      invalidate(unit, State::Fault, FaultCode::HomeTimeout);
      return;
    }
    if (activeHall != unit.rawHall) {
      unit.rawHall = activeHall;
      unit.hallChangedUs = nowUs;
    }
    const bool stable = elapsed(nowUs, unit.hallChangedUs, kHallDebounceUs);
    if (unit.state == State::ClearMarker && !activeHall) {
      // Pause on a candidate edge: debounce must not move the reference point.
      if (stable) unit.state = State::SeekMarker;
      return;
    }
    if (unit.state == State::SeekMarker && activeHall) {
      if (stable) {
        unit.state = State::Offset;
        unit.remaining = unit.config.homeOffsetSteps;
        if (unit.remaining == 0) finishHome(unit, nowUs);
      }
      return;
    }
    if (!elapsed(nowUs, unit.lastStepUs, unit.config.halfStepIntervalUs)) return;
    if (unit.homeSteps >= 2u * unit.config.stepsPerRevolution) {
      invalidate(unit, State::Fault, FaultCode::HomeStepLimit);
      return;
    }
    advancePhase(unit, nowUs);
    ++unit.homeSteps;
    if (unit.state == State::Offset && --unit.remaining == 0)
      finishHome(unit, nowUs);
    return;
  }
  if (unit.state == State::Moving &&
      elapsed(nowUs, unit.lastStepUs, unit.config.halfStepIntervalUs)) {
    advancePhase(unit, nowUs);
    // Direction changes only electrical phase order; logical motion remains
    // forward through the user's installed flap order.
    ++unit.position;
    if (--unit.remaining == 0) settle(unit, nowUs);
  }
}

void MotionController::tick(uint32_t nowUs, uint16_t activeHallMask) {
  for (uint8_t i = 0; i < kUnitCount; ++i) {
    if (ownsSlot(units_[i]))
      runUnit(units_[i], nowUs, (activeHallMask & (1u << i)) != 0);
  }
  uint8_t running = 0;
  for (uint8_t i = 0; i < kUnitCount; ++i)
    if (ownsSlot(units_[i])) ++running;

  // Round-robin admission avoids favouring low-numbered units in repeated
  // batches. Settling units retain their slot until their coils are released.
  const uint8_t first = nextScheduled_;
  for (uint8_t offset = 0; offset < kUnitCount && running < maxConcurrent_;
       ++offset) {
    const uint8_t i = static_cast<uint8_t>((first + offset) % kUnitCount);
    Unit& unit = units_[i];
    if (!unit.config.enabled) continue;
    if (unit.state == State::QueuedHome) {
      startHome(unit, nowUs, (activeHallMask & (1u << i)) != 0);
    } else if (unit.state == State::Ready && unit.pendingTarget) {
      startMove(unit, nowUs);
    } else {
      continue;
    }
    ++running;
    nextScheduled_ = static_cast<uint8_t>((i + 1) % kUnitCount);
  }
}

uint8_t MotionController::coilMask(uint8_t unit) const {
  return unit < kUnitCount ? units_[unit].coils : 0;
}

State MotionController::state(uint8_t unit) const {
  return unit < kUnitCount ? units_[unit].state : State::Fault;
}

FaultCode MotionController::fault(uint8_t unit) const {
  return unit < kUnitCount ? units_[unit].fault : FaultCode::InvalidUnit;
}

bool MotionController::homed(uint8_t unit) const {
  return unit < kUnitCount && units_[unit].positionValid;
}

bool MotionController::unitBusy(uint8_t unit) const {
  return unit < kUnitCount && (ownsSlot(units_[unit]) ||
          units_[unit].state == State::QueuedHome || units_[unit].pendingTarget);
}

bool MotionController::busy() const {
  for (uint8_t i = 0; i < kUnitCount; ++i)
    if (unitBusy(i)) return true;
  return false;
}

uint8_t MotionController::currentIndex(uint8_t unit) const {
  return unit < kUnitCount ? units_[unit].current : 0;
}

uint8_t MotionController::targetIndex(uint8_t unit) const {
  return unit < kUnitCount ? units_[unit].target : 0;
}

uint32_t MotionController::stepsFromZero(uint8_t unit) const {
  return unit < kUnitCount ? units_[unit].position : 0;
}

uint32_t MotionController::stepsForIndex(uint32_t stepsPerRevolution,
                                       uint8_t index) {
  // Absolute rounded positions avoid accumulating integer division error.
  return static_cast<uint32_t>((static_cast<uint64_t>(stepsPerRevolution) *
                                index + kFlapCount / 2u) / kFlapCount);
}

const char* MotionController::stateName(State state) {
  switch (state) {
    case State::Unhomed: return "unhomed";
    case State::QueuedHome: return "queued_home";
    case State::ClearMarker: return "clear_marker";
    case State::SeekMarker: return "seek_marker";
    case State::Offset: return "offset";
    case State::Ready: return "ready";
    case State::Moving: return "moving";
    case State::Settling: return "settling";
    case State::Fault: return "fault";
  }
  return "unknown";
}

const char* MotionController::faultName(FaultCode fault) {
  switch (fault) {
    case FaultCode::None: return "none";
    case FaultCode::HomeStepLimit: return "home_step_limit";
    case FaultCode::HomeTimeout: return "home_timeout";
    case FaultCode::OutputFault: return "output_fault";
    case FaultCode::BusError: return "bus_error";
    case FaultCode::InvalidUnit: return "invalid_unit";
  }
  return "unknown";
}

}  // namespace splitflap
