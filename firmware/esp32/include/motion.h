// SPDX-License-Identifier: GPL-3.0-only
#ifndef SPLITFLAP_MOTION_H
#define SPLITFLAP_MOTION_H

#include <stdint.h>

// Hardware-independent, allocation-free motion engine. The adapter must call
// tick frequently, read active-low Hall inputs as active=true, and apply all
// changed coilMask values together. Never call this object concurrently.
namespace splitflap {

constexpr uint8_t kUnitCount = 10;
constexpr uint8_t kFlapCount = 45;
constexpr uint16_t kAllUnitsMask = (1u << kUnitCount) - 1u;
constexpr uint32_t kHallDebounceUs = 5000;
constexpr uint32_t kSettleUs = 100000;

enum class State : uint8_t {
  Unhomed, QueuedHome, ClearMarker, SeekMarker, Offset,
  Ready, Moving, Settling, Fault
};

enum class FaultCode : uint8_t {
  None, HomeStepLimit, HomeTimeout, OutputFault, BusError, InvalidUnit
};

struct UnitConfig {
  bool enabled;
  uint32_t stepsPerRevolution;
  uint32_t homeOffsetSteps;
  uint32_t halfStepIntervalUs;
  int8_t phaseDirection;

  // 4096 is a provisional starting point, not measured drum calibration.
  UnitConfig()
      : enabled(false), stepsPerRevolution(4096), homeOffsetSteps(0),
        halfStepIntervalUs(8000), phaseDirection(1) {}
};

class MotionController {
 public:
  MotionController();

  static bool validConfig(const UnitConfig& config);
  // Changes are accepted only while the whole controller is idle. Even an
  // unchanged configuration invalidates this unit's position and releases it.
  bool configure(uint8_t unit, const UnitConfig& config);
  const UnitConfig& config(uint8_t unit) const;  // Invalid unit -> disabled defaults.
  bool setMaxConcurrent(uint8_t count);
  uint8_t maxConcurrent() const;
  // Disabling is an immediate stop; enabling requires an idle controller.
  bool setEnabled(uint8_t unit, bool enabled);

  // Rejects the entire request if a selected unit is disabled or busy.
  // Explicit home also recovers a stopped or faulted unit. A zero mask fails.
  bool beginHome(uint16_t mask);
  // Only accepts enabled, homed, idle units. A lower target queues a rehome
  // before moving to that target, bounding accumulated gearbox ratio error.
  bool setTarget(uint8_t unit, uint8_t index);
  // Supply a fresh, successfully read Hall sample on every tick. The adapter
  // must stop on input/output bus errors rather than reusing stale samples.
  // All timing is unsigned wrap-safe; calls must be less than 2^32 us apart.
  void tick(uint32_t nowUs, uint16_t activeHallMask);

  // Both invalidate position, discard pending work, and release coils at once.
  void stop(uint16_t mask = kAllUnitsMask);
  void faultAll(FaultCode code = FaultCode::OutputFault);

  uint8_t coilMask(uint8_t unit) const;  // Bit 0..3 -> verified logical PH1..PH4 wiring.
  State state(uint8_t unit) const;
  FaultCode fault(uint8_t unit) const;
  bool homed(uint8_t unit) const;
  bool busy() const;
  bool unitBusy(uint8_t unit) const;
  uint8_t currentIndex(uint8_t unit) const;  // Meaningful only when homed.
  uint8_t targetIndex(uint8_t unit) const;
  uint32_t stepsFromZero(uint8_t unit) const;

  static uint32_t stepsForIndex(uint32_t stepsPerRevolution, uint8_t index);
  static const char* stateName(State state);
  static const char* faultName(FaultCode fault);

 private:
  struct Unit {
    UnitConfig config;
    State state;
    FaultCode fault;
    bool positionValid;
    bool pendingTarget;
    bool phaseInitialized;
    bool rawHall;
    uint8_t current;
    uint8_t target;
    uint8_t phase;
    uint8_t coils;
    uint32_t position;
    uint32_t remaining;
    uint32_t homeSteps;
    uint32_t homeStartedUs;
    uint32_t homeTimeoutUs;
    uint32_t lastStepUs;
    uint32_t hallChangedUs;
    uint32_t settlingStartedUs;
    Unit();
  };

  Unit units_[kUnitCount];
  uint8_t maxConcurrent_;
  uint8_t nextScheduled_;

  static bool ownsSlot(const Unit& unit);
  static bool isHoming(State state);
  static bool elapsed(uint32_t now, uint32_t since, uint32_t duration);
  void invalidate(Unit& unit, State state, FaultCode fault);
  void startHome(Unit& unit, uint32_t nowUs, bool activeHall);
  void startMove(Unit& unit, uint32_t nowUs);
  void advancePhase(Unit& unit, uint32_t nowUs);
  void finishHome(Unit& unit, uint32_t nowUs);
  void settle(Unit& unit, uint32_t nowUs);
  void runUnit(Unit& unit, uint32_t nowUs, bool activeHall);
};

}  // namespace splitflap
#endif
