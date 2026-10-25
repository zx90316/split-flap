// SPDX-License-Identifier: GPL-3.0-only
#include "bench.h"
#include "pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
  c.intervalUs = 3000; c.order[1] = 2; c.order[2] = 1;
  assert(b.configure(c)); assert(b.arm()); assert(b.step(8, 0));
  const uint8_t expected[8] = {1, 5, 4, 6, 2, 10, 8, 9};
  for (int i = 0; i < 8; ++i) { b.tick((i + 1) * 3000, false); assert(b.coils() == expected[i]); }
  b.stop(); c.order[0] = 2;
  assert(!b.configure(c));
  c = mega::Config(); c.offset = c.revolution; assert(!b.configure(c));
  c = mega::Config(); c.direction = -1; assert(b.configure(c)); assert(b.arm());
  assert(b.step(2, 0)); b.tick(5000, false); assert(b.coils() == 1);
  b.tick(10000, false); assert(b.coils() == 9);
  b.stop(); assert(b.arm()); assert(b.led(0));
  for (uint32_t t = 1000; t <= 4000000; t += 1000) {
    b.tick(t, false);
    if (t % 1000000 == 0 && t < 4000000) assert(b.coils() == (1 << (t / 1000000)));
  }
  assert(!b.busy() && !b.coils() && !b.homed());
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
  puts("PASS bench: 8 suites (motion, homing, faults, latch, cycle, timing, phases, pins)");
}
