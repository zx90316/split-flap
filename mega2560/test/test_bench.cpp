// SPDX-License-Identifier: GPL-3.0-only
#include "bench.h"
#include "pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

struct Rotor {
  mega::Bench bench;
  uint32_t now = 1000;
  uint16_t physical = 120;
  int forced = -1;
  bool latching = false, latched = false;
  Rotor() {
    mega::Config c;
    c.revolution = 1000; c.intervalUs = 3000; c.offset = 17;
    assert(bench.configure(c));
    for (int i = 0; i < 10; ++i) tick();
  }
  void tick(uint32_t delta = 1000) {
    now += delta;
    bool hall = physical < 40;
    if (latching) {
      if (physical < 30) latched = true;
      if (physical >= 500 && physical < 530) latched = false;
      hall = latched;
    }
    if (forced >= 0) hall = forced != 0;
    const uint32_t before = bench.steps();
    bench.tick(now, hall);
    physical = (physical + bench.steps() - before) % 1000;
  }
  void run() {
    for (uint32_t i = 0; i < 200000 && bench.busy(); ++i) tick();
    assert(!bench.busy());
  }
  void home() { assert(bench.arm()); assert(bench.home(now)); run(); }
};

void bootAndStop() {
  Rotor r;
  assert(!r.bench.armed() && !r.bench.homed() && !r.bench.coils());
  assert(!r.bench.step(1, r.now));
  assert(r.bench.arm()); assert(r.bench.step(100, r.now)); r.run();
  assert(r.bench.steps() == 100 && !r.bench.coils() && !r.bench.homed());
  assert(r.bench.step(100, r.now));
  for (int i = 0; i < 30; ++i) r.tick();
  assert(r.bench.coils());
  r.bench.stop();
  const uint32_t stopped = r.bench.steps();
  for (int i = 0; i < 100; ++i) r.tick();
  assert(!r.bench.coils() && !r.bench.armed() && !r.bench.homed());
  assert(r.bench.steps() == stopped);
  assert(!r.bench.goTo(1, r.now));
}

void homingAndForwardWrap() {
  Rotor r;
  r.physical = 5;  // Boot inside the active marker: must leave and find it again.
  for (int i = 0; i < 10; ++i) r.tick();
  r.home();
  assert(r.bench.homed() && !r.bench.error() && r.bench.index() == 0);
  assert(r.bench.steps() > 950 && !r.bench.coils());
  assert(r.physical >= 17 && r.physical < 23);
  assert(r.bench.goTo(44, r.now)); r.run();
  assert(r.bench.index() == 44 && r.bench.homed());
  assert(r.bench.goTo(1, r.now)); r.run();
  assert(r.bench.index() == 1 && r.bench.homed() && !r.bench.coils());
  assert(r.bench.steps() > 40);  // Includes marker seek and offset on wrap.
  assert(r.physical >= 39 && r.physical < 46);
}

void hallFaultsAndDebounce() {
  for (int active = 0; active <= 1; ++active) {
    Rotor r; r.forced = active;
    for (int i = 0; i < 10; ++i) r.tick();
    r.home();
    assert(r.bench.error());
    assert(!strcmp(r.bench.error(), active ? "hall_did_not_release" : "hall_edge_missing"));
    assert(!r.bench.coils() && !r.bench.homed() && !r.bench.armed());
    assert(r.bench.steps() <= 1500);
  }
  Rotor bounce;
  bounce.forced = 0;
  assert(bounce.bench.arm()); assert(bounce.bench.home(bounce.now));
  for (int i = 0; i < 190000 && bounce.bench.busy(); ++i) {
    bounce.forced = i % 2; bounce.tick();  // 1 ms pulses cannot establish a marker.
  }
  assert(!bounce.bench.homed() && !bounce.bench.coils() && bounce.bench.error());
}

void measureAndLatch() {
  Rotor r;
  assert(r.bench.arm()); assert(r.bench.measure(r.now)); r.run();
  assert(r.bench.measuredCount() == 3 && !r.bench.homed() && !r.bench.coils());
  for (int i = 0; i < 3; ++i) assert(r.bench.measured(i) == 1000);
  Rotor latch;
  latch.latching = true; latch.latched = true;
  for (int i = 0; i < 10; ++i) latch.tick();
  latch.home();
  assert(latch.bench.homed() && !latch.bench.error());
  assert(latch.bench.measure(latch.now)); latch.run();
  for (int i = 0; i < 3; ++i) assert(latch.bench.measured(i) == 1000);
}

void fullCycleAndRounding() {
  Rotor r; r.home();
  assert(r.bench.cycle(r.now));
  bool seen[45] = {};
  for (int i = 0; i < 100000 && r.bench.busy(); ++i) {
    r.tick();
    if (r.bench.mode() == mega::Mode::Pause) seen[r.bench.index()] = true;
  }
  for (int i = 0; i < 45; ++i) assert(seen[i]);
  assert(!r.bench.busy() && !r.bench.coils() && r.bench.homed());
  assert(r.bench.index() == 0 && !r.bench.error());
  assert(r.physical >= 17 && r.physical < 23);
  for (int i = 0; i < 45; ++i) {
    const int expected = (4076 * i + 22) / 45;
    assert(mega::Bench::positionFor(4076, i) == expected);
  }
}

void schedulingAndLimits() {
  Rotor r;
  assert(r.bench.arm()); assert(r.bench.step(100, r.now));
  r.tick(50000); assert(r.bench.steps() == 1);  // Never backfill 16 steps.
  r.tick(100001);
  assert(r.bench.error() && !strcmp(r.bench.error(), "loop_stall"));
  assert(!r.bench.coils() && !r.bench.armed());
  Rotor wrap;
  wrap.now = UINT32_MAX - 10000;
  assert(wrap.bench.arm()); assert(wrap.bench.step(100, wrap.now)); wrap.run();
  assert(wrap.bench.steps() == 100 && !wrap.bench.error());
  Rotor timeout;
  mega::Config c = timeout.bench.config(); c.intervalUs = 30000;
  assert(timeout.bench.configure(c)); assert(timeout.bench.arm());
  assert(timeout.bench.step(20000, timeout.now));
  for (int i = 0; i < 7000 && timeout.bench.busy(); ++i) timeout.tick(30000);
  assert(timeout.bench.error() && !strcmp(timeout.bench.error(), "motion_timeout"));
  assert(!timeout.bench.coils() && !timeout.bench.armed());
  assert(!timeout.bench.step(0, timeout.now));
  assert(!timeout.bench.goTo(45, timeout.now));
}

void phaseConfigurationAndLeds() {
  mega::Bench b;
  mega::Config c;
  c.intervalUs = 10000; c.order[1] = 2; c.order[2] = 1;
  assert(b.configure(c)); assert(b.arm()); assert(b.step(8, 0));
  const uint8_t expected[8] = {1, 5, 4, 6, 2, 10, 8, 9};
  for (int i = 0; i < 8; ++i) { b.tick((i + 1) * 10000, false); assert(b.coils() == expected[i]); }
  b.stop(); c.order[0] = 2;
  assert(!b.configure(c));
  c = mega::Config(); c.offset = c.revolution; assert(!b.configure(c));
  c = mega::Config(); c.direction = -1; c.intervalUs = 10000;
  assert(b.configure(c)); assert(b.arm());
  assert(b.step(2, 0)); b.tick(10000, false); assert(b.coils() == 1);
  b.tick(20000, false); assert(b.coils() == 9);
  b.stop(); assert(b.arm()); assert(b.led(0));
  for (uint32_t t = 1000; t <= 4000000; t += 1000) {
    b.tick(t, false);
    if (t % 1000000 == 0 && t < 4000000) assert(b.coils() == (1 << (t / 1000000)));
  }
  assert(!b.busy() && !b.coils() && !b.homed());
}

// Observe actual scheduled output times, without reading the planner's internals.
std::vector<uint32_t> intervals(Rotor& r) {
  std::vector<uint32_t> result;
  uint32_t last = r.now;
  for (uint32_t ticks = 0; r.bench.busy() && ticks < 4000000; ++ticks) {
    const uint32_t before = r.bench.steps();
    r.tick(50);
    if (r.bench.steps() != before) {
      assert(r.bench.steps() == before + 1);  // At most one step per tick.
      result.push_back(static_cast<uint32_t>(r.now - last));
      last = r.now;
    }
  }
  assert(!r.bench.busy() && !r.bench.error() && !r.bench.coils());
  return result;
}

void assertRamp(const std::vector<uint32_t>& times, uint16_t cruise) {
  assert(!times.empty() && times.front() == 10000 && times.back() == 10000);
  for (size_t i = 0; i < times.size(); ++i) {
    assert(times[i] >= cruise && times[i] <= 10000);
    assert(times[i] == times[times.size() - 1 - i]);
    if (i && i <= (times.size() - 1) / 2) assert(times[i] <= times[i - 1]);
    if (i > times.size() / 2) assert(times[i] >= times[i - 1]);
  }
}

void accelerationProfiles() {
  const uint16_t counts[] = {1, 2, 3, 8, 91, 500, 1500, 20000};
  for (uint16_t count : counts) {
    Rotor r;
    mega::Config c = r.bench.config(); c.intervalUs = 1500;
    if (count == 20000) c.acceleration = 6000;  // Largest distance/rate product.
    assert(r.bench.configure(c)); assert(r.bench.arm());
    assert(r.bench.step(count, r.now));
    const std::vector<uint32_t> times = intervals(r);
    assert(times.size() == count && r.bench.steps() == count);
    assertRamp(times, 1500);
    if (count >= 3) assert(times[count / 2] < times.front());
    if (count == 500) assert(times[count / 2] > 1500);  // Triangle, no cruise.
    if (count >= 1500) assert(times[count / 2] == 1500);  // Trapezoid, cruises.
    if (count == 1500) {
      uint32_t duration = 0;
      for (uint32_t interval : times) duration += interval;
      assert(duration > 2250000 && duration < 5000000);
    }
  }
  for (uint16_t cruise : {uint16_t(10000), uint16_t(12000), uint16_t(30000)}) {
    Rotor r; mega::Config c = r.bench.config(); c.intervalUs = cruise;
    assert(r.bench.configure(c)); assert(r.bench.arm()); assert(r.bench.step(8, r.now));
    for (uint32_t interval : intervals(r)) assert(interval == cruise);
  }
  Rotor slow, fast;
  mega::Config c = slow.bench.config(); c.intervalUs = 1500; c.acceleration = 100;
  assert(slow.bench.configure(c)); assert(slow.bench.arm()); assert(slow.bench.step(91, slow.now));
  c.acceleration = 2000;
  assert(fast.bench.configure(c)); assert(fast.bench.arm()); assert(fast.bench.step(91, fast.now));
  const std::vector<uint32_t> gentle = intervals(slow), rapid = intervals(fast);
  assertRamp(gentle, 1500); assertRamp(rapid, 1500);
  assert(rapid[45] < gentle[45]);
}

void rampedDisplayAndSlowReference() {
  Rotor r;
  r.home();
  assert(r.bench.goTo(1, r.now));
  const std::vector<uint32_t> shortMove = intervals(r);
  assert(shortMove.size() == mega::Bench::positionFor(1000, 1));
  assertRamp(shortMove, 3000);
  assert(r.bench.homed() && r.bench.index() == 1);
  assert(r.bench.goTo(44, r.now));
  const std::vector<uint32_t> longMove = intervals(r);
  assert(longMove.size() == mega::Bench::positionFor(1000, 44) - shortMove.size());
  assertRamp(longMove, 3000);
  assert(longMove[longMove.size() / 2] == 3000);
  assert(r.bench.goTo(44, r.now));
  assert(intervals(r).empty() && r.bench.index() == 44);  // No zero-length underflow.
  // Cruise changes below the reference cap must leave homing/offset identical.
  Rotor baseline, tuned;
  mega::Config c = baseline.bench.config(); c.intervalUs = 5000;
  assert(baseline.bench.configure(c));
  c.intervalUs = 1500;
  assert(tuned.bench.configure(c));
  baseline.home(); tuned.home();
  assert(baseline.bench.steps() == tuned.bench.steps());
  assert(baseline.physical == tuned.physical && baseline.now == tuned.now);
  assert(baseline.bench.measure(baseline.now)); assert(tuned.bench.measure(tuned.now));
  uint32_t previousStep = tuned.now;
  while (tuned.bench.busy()) {
    const uint32_t before = tuned.bench.steps();
    tuned.tick(50);
    if (before != tuned.bench.steps()) {
      assert(static_cast<uint32_t>(tuned.now - previousStep) >= 5000);
      previousStep = tuned.now;
    }
  }
  baseline.run();
  assert(tuned.bench.measuredCount() == 3 && !tuned.bench.homed() && !tuned.bench.error());
  for (int i = 0; i < 3; ++i)
    assert(tuned.bench.measured(i) == 1000 && tuned.bench.measured(i) == baseline.bench.measured(i));
}

void rampLimitsAndImmediateStops() {
  mega::Bench b;
  mega::Config c;
  c.intervalUs = 799; assert(!b.configure(c));
  c.intervalUs = 30001; assert(!b.configure(c));
  c.intervalUs = 1500; c.acceleration = 99; assert(!b.configure(c));
  c.acceleration = 6001; assert(!b.configure(c));
  c.acceleration = 100; assert(b.configure(c)); assert(b.arm());
  c.acceleration = 6000; assert(b.configure(c)); assert(!b.armed());
  for (uint16_t stopAt : {uint16_t(1), uint16_t(150), uint16_t(750), uint16_t(1400)}) {
    Rotor r; c = r.bench.config(); c.intervalUs = 1500;
    assert(r.bench.configure(c)); assert(r.bench.arm()); assert(r.bench.step(1500, r.now));
    while (r.bench.steps() < stopAt) r.tick(50);
    r.bench.stop();
    assert(!r.bench.coils() && !r.bench.busy() && !r.bench.armed() && !r.bench.homed());
    for (int i = 0; i < 100; ++i) r.tick();
    assert(r.bench.steps() == stopAt);
    assert(r.bench.arm()); assert(r.bench.step(8, r.now));
    assertRamp(intervals(r), 1500);  // Restart from slow speed, no stale ramp.
  }
}

void highSpeedProfiles() {
  for (uint16_t cruise : {uint16_t(800), uint16_t(1000), uint16_t(1200)}) {
    Rotor r; mega::Config c = r.bench.config();
    c.intervalUs = cruise; c.acceleration = 800;
    assert(r.bench.configure(c)); assert(r.bench.arm()); assert(r.bench.step(4096, r.now));
    const std::vector<uint32_t> times = intervals(r);
    assert(times.size() == 4096); assertRamp(times, cruise);
    assert(times[2048] == cruise);
  }
  Rotor gentle, quick;
  mega::Config c = gentle.bench.config(); c.intervalUs = 1500; c.acceleration = 400;
  assert(gentle.bench.configure(c)); assert(gentle.bench.arm()); assert(gentle.bench.step(91, gentle.now));
  c.acceleration = 6000;
  assert(quick.bench.configure(c)); assert(quick.bench.arm()); assert(quick.bench.step(91, quick.now));
  const std::vector<uint32_t> slow = intervals(gentle), fast = intervals(quick);
  assertRamp(slow, 1500); assertRamp(fast, 1500);
  assert(slow[45] > 1500 && fast[45] == 1500);
}

void runManual(mega::Bench& b, uint32_t& now, bool* seen = nullptr) {
  for (uint32_t guard = 0; b.busy() && guard < 30000; ++guard) {
    assert(b.manual() && !b.homed());
    assert(b.mode() == mega::Mode::Moving || b.mode() == mega::Mode::Settle ||
        b.mode() == mega::Mode::Pause);
    if (seen && b.mode() == mega::Mode::Settle) seen[b.index()] = true;
    now += 5000;
    b.tick(now, (now / 15000) % 2);  // Disconnected/noisy Hall cannot affect manual moves.
  }
  assert(!b.busy() && !b.coils() && !b.error() && b.manual() && !b.homed());
}

void manualAllTargetsAndWrap() {
  for (uint16_t revolution : {uint16_t(4076), uint16_t(4096)}) {
    mega::Bench b;
    mega::Config c; c.revolution = revolution;
    if (revolution == 4076) c.direction = -1;
    assert(b.configure(c));
    uint32_t now = 0;
    for (uint8_t from = 0; from < 45; ++from) {
      for (uint8_t to = 0; to < 45; ++to) {
        assert(b.setCurrent(from));
        assert(b.positioned() && b.manual() && !b.homed() && !b.armed() && !b.coils());
        assert(b.index() == from && !b.goTo(to, now));
        assert(b.arm()); assert(b.goTo(to, now));
        runManual(b, now);
        const int start = mega::Bench::positionFor(revolution, from);
        const int end = mega::Bench::positionFor(revolution, to);
        assert(b.steps() == static_cast<uint32_t>((end - start + revolution) % revolution));
        assert(b.index() == to && b.armed());
      }
    }
    // Consecutive targets must use the newly reached flap, with no SET between.
    assert(b.setCurrent(0)); assert(b.arm());
    uint32_t accumulated = 0;
    for (uint8_t target = 1; target <= 45; ++target) {
      assert(b.goTo(target % 45, now)); runManual(b, now);
      accumulated += b.steps();
      assert(b.index() == target % 45);
    }
    assert(accumulated == revolution && b.index() == 0);
  }
  for (uint16_t revolution : {uint16_t(1000), uint16_t(20000)}) {
    mega::Bench b; mega::Config c; c.revolution = revolution;
    assert(b.configure(c)); assert(b.setCurrent(44)); assert(b.arm());
    uint32_t now = UINT32_MAX - 10000;
    assert(b.goTo(0, now)); runManual(b, now);
    assert(b.steps() == revolution - mega::Bench::positionFor(revolution, 44));
  }
}

void manualCycleAndInvalidation() {
  for (uint8_t start : {uint8_t(0), uint8_t(17), uint8_t(44)}) {
    mega::Bench b; mega::Config c; c.revolution = 4076;
    assert(b.configure(c)); assert(b.setCurrent(start)); assert(b.arm());
    uint32_t now = 0;
    bool seen[45] = {};
    assert(b.cycle(now)); runManual(b, now, seen);
    for (bool visited : seen) assert(visited);
    assert(b.index() == start && b.steps() == 4076);
    assert(b.goTo((start + 1) % 45, now));
    assert(!b.setCurrent(0));  // A direct core call cannot relabel a moving rotor.
    b.tick(now += 10000, false);
    b.stop();
    assert(!b.positioned() && !b.manual() && !b.homed() && !b.armed() && !b.coils());
    assert(b.arm() && !b.goTo(0, now));
    assert(b.setCurrent(30)); assert(!b.setCurrent(45)); assert(b.index() == 30);
    assert(b.arm()); assert(b.step(1, now));
    assert(!b.positioned());
    while (b.busy()) b.tick(now += 5000, true);
    assert(b.setCurrent(0));
    c.intervalUs = 800; assert(b.configure(c));
    assert(!b.positioned() && !b.armed());
    assert(b.setCurrent(0)); assert(b.arm()); assert(b.led(now));
    assert(!b.positioned()); b.stop();
  }
  Rotor r;
  assert(r.bench.setCurrent(3)); r.home();
  assert(r.bench.homed() && !r.bench.manual());  // Explicit HOME restores sensor mode.
}

void displayValues() {
  uint8_t index = 255;
  assert(mega::indexFor("BLANK", index) && index == 0);
  assert(mega::indexFor("SPACE", index) && index == 0);
  assert(mega::indexFor("AE", index) && index == 27);
  assert(mega::indexFor("OE", index) && index == 28);
  assert(mega::indexFor("UE", index) && index == 29);
  assert(mega::indexFor("0", index) && index == 30);
  assert(mega::indexFor("9", index) && index == 39);
  for (uint8_t slot = 1; slot < 45; ++slot) {
    const char glyph[] = {mega::characterFor(slot), 0};
    assert(mega::indexFor(glyph, index) && index == slot);
  }
  for (const char* invalid : {"", "10", "AB", "_", "/", "UNKNOWN"})
    assert(!mega::indexFor(invalid, index));
  assert(!mega::indexFor(nullptr, index));
  assert(mega::characterFor(45) == 0);
}

void pinAllocation() {
  bool used[70] = {};
  for (int u = 0; u < 10; ++u) {
    for (int p = 0; p < 4; ++p) {
      const int pin = mega::kMotorPins[u][p];
      assert(pin >= 2 && pin < 54 && !used[pin]); used[pin] = true;
      assert(pin < 18 || pin > 21);
    }
    const int hall = mega::kHallPins[u];
    assert(hall == 54 + u && !used[hall]); used[hall] = true;
  }
  assert(mega::kMotorPins[9][0] == 8 && mega::kMotorPins[9][3] == 11);
}

int main() {
  bootAndStop(); homingAndForwardWrap(); hallFaultsAndDebounce(); measureAndLatch();
  fullCycleAndRounding(); schedulingAndLimits(); phaseConfigurationAndLeds(); pinAllocation();
  accelerationProfiles(); rampedDisplayAndSlowReference(); rampLimitsAndImmediateStops();
  highSpeedProfiles(); manualAllTargetsAndWrap(); manualCycleAndInvalidation(); displayValues();
  puts("PASS bench: 15 suites (motion, homing, faults, latch, cycle, timing, phases, pins, ramps, reference, ramp stops, fast cruise, manual targets, manual cycle, values)");
}
