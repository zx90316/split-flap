// SPDX-License-Identifier: GPL-3.0-only
#include "panel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void outputs(const mega::Panel& panel) {
  uint8_t energized = 0;
  for (uint8_t u = 0; u < 10; ++u) {
    if (panel.unit(u).coils()) { assert(panel.enabled(u)); ++energized; }
  }
  assert(energized <= panel.digits());
}

void drain(mega::Panel& panel, uint32_t& now) {
  for (uint32_t i = 0; panel.busy() && i < 1000000; ++i) {
    outputs(panel); panel.tick(now += 5000, 0x3ff);
  }
  outputs(panel);
  assert(!panel.busy() && !panel.error());
}

void widthsAndText() {
  for (uint8_t count = 1; count <= 10; ++count) {
    mega::Panel panel;
    assert(panel.digits() == 1 && panel.enabled(0) && !panel.enabled(1));
    assert(panel.setDigits(count));
    uint8_t current[10] = {}, target[10] = {};
    for (uint8_t i = 0; i < count; ++i) target[i] = i + 1;
    assert(panel.setCurrentText(current, count)); assert(panel.armAll());
    uint32_t now = UINT32_MAX - 10000;
    assert(panel.text(target, count, now)); drain(panel, now);
    for (uint8_t u = 0; u < 10; ++u) {
      assert(panel.enabled(u) == (u < count));
      if (u < count) assert(panel.unit(u).index() == target[u] && panel.unit(u).manual());
      else assert(!panel.unit(u).armed() && panel.unit(u).steps() == 0);
    }
    assert(panel.text(target, count, now)); assert(!panel.busy());  // Unchanged text is immediate.
    assert(panel.text(current, count, now)); drain(panel, now);  // Every digit wraps without Hall.
    for (uint8_t u = 0; u < count; ++u) assert(panel.unit(u).index() == 0);
  }
}

void installationMapsAndState() {
  mega::Panel panel;
  assert(!panel.setDigits(0) && !panel.setDigits(11) && !panel.select(9));
  const uint8_t units[] = {9, 0, 4}, duplicate[] = {0, 0}, invalid[] = {0, 10};
  assert(panel.setUnits(units, 3) && panel.select(9));
  mega::Config c = panel.selected().config(); c.revolution = 4076; c.intervalUs = 800;
  assert(panel.selected().configure(c)); assert(panel.setPolarity(false));
  assert(panel.selected().setCurrent(26)); assert(panel.selected().arm());
  assert(panel.select(0)); assert(panel.selected().config().revolution == 4096);
  assert(panel.select(9)); assert(panel.selected().index() == 26 && panel.selected().armed());
  assert(panel.selected().config().revolution == 4076 && !panel.activeLow(9) && panel.activeLow(0));
  assert(!panel.setUnits(duplicate, 2) && !panel.setUnits(invalid, 2));
  assert(!panel.setUnits(nullptr, 1) && !panel.setUnits(units, 0));
  assert(panel.digits() == 3 && panel.selected().positioned());
  const uint8_t current[] = {1, 2, 3}, target[] = {3, 2, 1};
  assert(panel.setCurrentText(current, 3)); assert(panel.armAll());
  uint32_t now = 0; assert(panel.text(target, 3, now)); drain(panel, now);
  assert(panel.unit(9).index() == 3 && panel.unit(0).index() == 2 && panel.unit(4).index() == 1);
  assert(panel.unit(1).steps() == 0);
  assert(panel.setDigits(2) && panel.selectedUnit() == 0 && !panel.select(9));
  assert(panel.unit(9).config().revolution == 4076 && !panel.unit(9).positioned());
}

void atomicValidationAndSettings() {
  mega::Panel panel; assert(panel.setDigits(5));
  const uint8_t current[5] = {}, target[] = {1, 2, 3, 4, 5}, invalid[] = {1, 2, 3, 4, 45};
  assert(panel.setCurrentText(current, 5));
  assert(!panel.text(target, 5, 0));  // No arm: nobody may start.
  assert(panel.armAll());
  panel.unit(4).stop();
  assert(!panel.text(target, 5, 0) && !panel.unit(0).busy());
  assert(panel.armAll());
  assert(!panel.text(target, 5, 0));  // Last unit has no position: no partial update.
  assert(panel.setCurrentText(current, 5) && panel.armAll());
  assert(!panel.setCurrentText(invalid, 5) && panel.unit(0).armed());
  assert(!panel.text(invalid, 5, 0) && !panel.text(target, 4, 0));
  assert(!panel.speedAll(799) && !panel.accelerationAll(6001));
  for (uint8_t u = 0; u < 5; ++u) assert(panel.unit(u).positioned() && panel.unit(u).armed());
  assert(panel.speedAll(800) && panel.accelerationAll(800));
  for (uint8_t u = 0; u < 5; ++u) {
    assert(panel.unit(u).config().intervalUs == 800 && panel.unit(u).config().acceleration == 800);
    assert(panel.unit(u).positioned() && panel.unit(u).armed());
  }
  assert(panel.unit(5).config().intervalUs == 800);
  assert(panel.setCurrentText(current, 5) && panel.armAll());
  uint32_t now = 0; assert(panel.text(target, 5, now));
  assert(!panel.setDigits(3) && !panel.select(1) && !panel.speedAll(1000));
  drain(panel, now);
}

void stopAndFaultCancelQueue() {
  mega::Panel panel; assert(panel.setDigits(3));
  const uint8_t current[3] = {}, target[] = {20, 21, 22};
  assert(panel.setCurrentText(current, 3) && panel.armAll());
  uint32_t now = 0; assert(panel.text(target, 3, now));
  panel.tick(now += 10000, 0x3ff); assert(panel.unit(0).steps() == 1);
  panel.stopAll();
  for (uint16_t i = 0; i < 1000; ++i) panel.tick(now += 5000, 0x3ff);
  assert(!panel.busy() && panel.unit(1).steps() == 1 && panel.unit(2).steps() == 1);
  for (uint8_t u = 0; u < 3; ++u) assert(!panel.unit(u).positioned() && !panel.unit(u).armed());
  assert(panel.armAll());
  mega::Config c = panel.unit(0).config(); c.revolution = 1000;
  assert(panel.unit(0).configure(c)); assert(panel.armAll());
  assert(panel.homeAll(now));
  for (uint32_t i = 0; panel.busy() && i < 10000; ++i) panel.tick(now += 5000, 0x3ff);
  assert(!panel.busy() && panel.error() && !strcmp(panel.error(), "hall_edge_missing"));
  assert(panel.faultUnit() == 0 && panel.unit(1).steps() > 0);
  outputs(panel);
  assert(panel.setCurrentText(current, 3) && panel.armAll());
  assert(panel.text(target, 3, now)); panel.tick(now += 100001, 0x3ff);
  assert(!panel.busy() && !strcmp(panel.error(), "loop_stall"));
  mega::Panel sparse;
  const uint8_t mapping[] = {7, 2}, blank[] = {0, 0}, next[] = {0, 1};
  assert(sparse.setUnits(mapping, 2) && sparse.setCurrentText(blank, 2) && sparse.armAll());
  assert(sparse.text(next, 2, now));  // First mapped unit is unchanged and skipped.
  sparse.tick(now + 100001, 0x3ff);
  assert(!sparse.busy() && sparse.faultUnit() == 2 && !strcmp(sparse.error(), "loop_stall"));
}

void batchCyclesAndHoming() {
  mega::Panel panel; assert(panel.setDigits(3));
  assert(panel.select(2) && panel.setPolarity(false) && panel.select(0));
  const uint8_t current[] = {0, 17, 44};
  assert(panel.setCurrentText(current, 3) && panel.armAll());
  uint32_t now = 0; assert(panel.cycleAll(now)); drain(panel, now);
  for (uint8_t u = 0; u < 3; ++u) assert(panel.unit(u).index() == current[u] && panel.unit(u).steps() == 4096);
  // Simulate an independent marker for each concurrently homing motor.
  assert(panel.homeAll(now));
  uint16_t physical[3] = {120, 5, 3300};
  for (uint32_t guard = 0; panel.busy() && guard < 100000; ++guard) {
    uint16_t mask = 0; uint32_t before[3];
    for (uint8_t u = 0; u < 3; ++u) {
      before[u] = panel.unit(u).steps();
      const bool active = physical[u] < 40;
      if (panel.activeLow(u) ? !active : active) mask |= 1U << u;
    }
    outputs(panel); panel.tick(now += 1000, mask);
    for (uint8_t u = 0; u < 3; ++u) {
      // A newly started job resets its own step count before its first step.
      const uint32_t after = panel.unit(u).steps();
      if (after == before[u] + 1) physical[u] = (physical[u] + 1) % 4096;
    }
  }
  assert(!panel.busy() && !panel.error());
  for (uint8_t u = 0; u < 3; ++u) assert(panel.unit(u).homed() && panel.unit(u).index() == 0);
}

void bulkGlyphs() {
  uint8_t values[10]; memset(values, 77, sizeof(values));
  assert(mega::parseText("A 0_!", 5, true, values));
  assert(values[0] == 1 && values[1] == 0 && values[2] == 30 && values[3] == 0 && values[4] == 44);
  assert(mega::parseText("$&#", 5, false, values));
  assert(values[0] == 27 && values[1] == 28 && values[2] == 29 && values[3] == 0 && values[4] == 0);
  assert(mega::parseText("", 10, false, values));
  for (uint8_t v : values) assert(v == 0);
  memset(values, 77, sizeof(values));
  assert(!mega::parseText("AB/CD", 5, true, values));
  for (uint8_t v : values) assert(v == 77);  // Invalid trailing glyph leaves output unchanged.
  assert(!mega::parseText("ABC", 5, true, values));
  assert(!mega::parseText("TOO LONG", 5, false, values));
  assert(!mega::parseText(nullptr, 5, false, values));
  assert(!mega::parseText("A", 0, false, values) && !mega::parseText("A", 11, false, values));
}

void concurrentTimingAndConfiguration() {
  for (uint8_t count = 1; count <= 10; ++count) {
    mega::Panel panel; assert(panel.setDigits(count));
    uint8_t values[10] = {}, target[10]; memset(target, 10, sizeof(target));
    assert(panel.setCurrentText(values, count) && panel.armAll());
    assert(panel.text(target, count, 0)); panel.tick(10000, 0x3ff);
    for (uint8_t u = 0; u < count; ++u) assert(panel.unit(u).steps() == 1 && panel.unit(u).coils());
    uint32_t now = 10000; drain(panel, now);
    assert(now < 7000000);  // Width ten takes one rotor's time, not ten rotations in sequence.
    sf::Settings config; panel.settings(config); config.units[count - 1].speed = 1500;
    assert(panel.applySettings(config));
    for (uint8_t u = 0; u < count; ++u) assert(panel.unit(u).index() == 10 && panel.unit(u).positioned() && panel.unit(u).armed());
    config.units[count - 1].revolution = 4076; assert(panel.applySettings(config));
    assert(!panel.unit(count - 1).positioned() && !panel.unit(count - 1).armed());
    for (uint8_t u = 0; u + 1 < count; ++u) assert(panel.unit(u).positioned() && panel.unit(u).armed());
  }
}
int main() {
  widthsAndText(); installationMapsAndState(); atomicValidationAndSettings();
  stopAndFaultCancelQueue(); batchCyclesAndHoming(); bulkGlyphs(); concurrentTimingAndConfiguration();
  puts("PASS panel: 7 suites; parallel widths 1..10, unchanged digits, sparse maps, simultaneous stop/fault, configuration preserves unaffected positions");
}
