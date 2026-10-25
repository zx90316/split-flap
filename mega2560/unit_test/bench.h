// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include "display.h"

namespace mega {
struct Config {
  uint16_t revolution = 4096;  // Initial HALF-step estimate, not calibration.
  uint16_t offset = 0;
  uint16_t intervalUs = 5000;  // Cruise interval; smaller is faster.
  uint16_t acceleration = 400;  // Half-steps/s^2, shared by acceleration/braking.
  int8_t direction = 1;
  uint8_t order[4] = {0, 1, 2, 3};
};

enum class Mode : uint8_t {
  Idle, Raw, Led, HomeStart, Clear, Seek, Offset, Moving, Measure, Settle, Pause
};
enum class Job : uint8_t { Raw, Led, Home, Move, Measure, Cycle };
enum class Reference : uint8_t { None, Manual, Hall };

// Pure, non-blocking single-unit state machine. All timestamps are micros().
class Bench {
 public:
  static const uint16_t kMinIntervalUs = 800;
  static const uint16_t kMaxIntervalUs = 30000;
  static const uint16_t kStartIntervalUs = 10000;
  static const uint16_t kReferenceIntervalUs = 5000;
  static const uint32_t kDebounceUs = 5000;
  static const uint32_t kStallUs = 100000;
  static const uint32_t kTimeoutUs = 180000000;
  bool configure(const Config& config);
  const Config& config() const { return config_; }
  bool arm();
  void stop();
  bool setCurrent(uint8_t index);
  bool step(uint16_t count, uint32_t now);
  bool led(uint32_t now);
  bool home(uint32_t now);
  bool measure(uint32_t now);
  bool goTo(uint8_t index, uint32_t now);
  bool cycle(uint32_t now);
  void tick(uint32_t now, bool activeHall);
  bool busy() const { return mode_ != Mode::Idle; }
  bool armed() const { return armed_; }
  bool homed() const { return reference_ == Reference::Hall; }
  bool manual() const { return reference_ == Reference::Manual; }
  bool positioned() const { return reference_ != Reference::None; }
  bool hall() const { return stableHall_; }
  uint8_t coils() const { return coils_; }
  uint8_t index() const { return index_; }
  uint32_t steps() const { return totalSteps_; }
  uint8_t measuredCount() const { return measuredCount_; }
  uint16_t measured(uint8_t i) const { return i < 3 ? measured_[i] : 0; }
  const char* error() const { return error_; }
  Mode mode() const { return mode_; }
  static uint16_t positionFor(uint16_t revolution, uint8_t index) {
    return static_cast<uint16_t>((static_cast<uint32_t>(revolution) * index + kFlapCount / 2) / kFlapCount);
  }

 private:
  bool begin(Job job, uint32_t now);
  void startHome(uint32_t now);
  void markerFound(uint32_t now);
  void homeDone(uint32_t now);
  void startMove(uint8_t target, uint32_t now);
  void startRamp(uint16_t count, uint32_t now);
  void updateRampInterval();
  uint16_t referenceInterval() const;
  void settle(uint32_t now);
  void advance(uint32_t now);
  void fail(const char* reason);
  Config config_;
  Mode mode_ = Mode::Idle;
  Job job_ = Job::Raw;
  Reference reference_ = Reference::None;
  bool armed_ = false, phaseKnown_ = false;
  bool hallKnown_ = false, rawHall_ = false, stableHall_ = false;
  bool tickKnown_ = false, markerCleared_ = false, cycleFinal_ = false;
  uint8_t phase_ = 0, coils_ = 0, index_ = 0, target_ = 0, ledIndex_ = 0;
  uint8_t measuredCount_ = 0, cycleStart_ = 0;
  uint16_t measured_[3] = {0, 0, 0};
  uint16_t remaining_ = 0, position_ = 0;
  uint16_t rampSteps_ = 0, rampIntervalUs_ = kStartIntervalUs;
  uint32_t startedAt_ = 0, lastStepAt_ = 0, stageAt_ = 0;
  uint32_t hallChangedAt_ = 0, lastTickAt_ = 0;
  uint32_t totalSteps_ = 0, stageSteps_ = 0, markerStep_ = 0;
  const char* error_ = nullptr;
};
}
