// SPDX-License-Identifier: GPL-3.0-only
#include "Arduino.h"
#include <assert.h>
#include <stdio.h>
uint32_t fakeNow = 0;
uint8_t fakePins[70] = {}, fakeModes[70] = {};
FakeSerial Serial;
#include "../unit_test/unit_test.ino"

void poll() { fakeNow += 1000; loop(); }
void send(const std::string& text) {
  Serial.feed(text);
  do { poll(); } while (Serial.available());
}
void off() {
  for (int u = 0; u < 10; ++u)
    for (int p = 0; p < 4; ++p) assert(fakePins[mega::kMotorPins[u][p]] == LOW);
}

void finish() {
  for (uint32_t i = 0; bench.busy() && i < 100000; ++i) poll();
  assert(!bench.busy() && !bench.error()); off();
}

int main() {
  setup(); off(); assert(!bench.armed());
  assert(Serial.output.find("SPEED 800..30000") != std::string::npos);
  assert(Serial.output.find("ACCEL 100..6000") != std::string::npos);
  send("SPEED 1500\nACCEL 600\nSTATUS\n"); off();
  assert(bench.config().intervalUs == 1500 && bench.config().acceleration == 600);
  assert(Serial.output.find("SPEED=1500 ACCEL=600") != std::string::npos);
  send("SPEED 799\nSPEED 30001\nACCEL 0\nACCEL 99\nACCEL 6001\nACCEL -1\nACCEL 65536\n");
  assert(bench.config().intervalUs == 1500 && bench.config().acceleration == 600);
  send("STEP 10\n"); off(); assert(!bench.busy());
  send("ARM\r\nSTEP 100\r\n");
  assert(bench.busy());  // Trailing CRLF must not abort a valid command.
  for (int i = 0; i < 20; ++i) poll();
  assert(bench.coils());
  send("STOP\r\n"); off();
  assert(!bench.busy() && !bench.armed() && !bench.homed());
  send("ARM\nSTEP 65536\n"); off(); assert(!bench.busy());
  send("STEP -1\nSTEP 0\nGOTO 10\nORDER 1124\n"); off();
  assert(!bench.busy());
  send(std::string(80, 'X') + "\n"); off(); assert(!bench.armed());
  send("ORDER 1324\nDIR -1\nOFFSET 12\nREVSTEPS 4076\nSPEED 8000\n");
  assert(bench.config().order[1] == 2 && bench.config().direction == -1);
  assert(bench.config().offset == 12 && bench.config().revolution == 4076);
  assert(!bench.armed());
  send("ARM\nACCEL 400\n"); off(); assert(!bench.armed() && !bench.homed());
  send("UNIT 9\nARM\nSTEP 10\n");
  assert(bench.config().intervalUs == 5000 && bench.config().acceleration == 400);
  for (int i = 0; i < 1000 && bench.busy(); ++i) {
    poll();
    for (int u = 0; u < 9; ++u)
      for (int p = 0; p < 4; ++p) assert(fakePins[mega::kMotorPins[u][p]] == LOW);
  }
  assert(!bench.busy() && bench.steps() == 10); off();
  send("WATCH\n"); assert(watching);
  fakePins[mega::kHallPins[9]] = LOW; poll();
  assert(Serial.output.find("raw=LOW U9=1") != std::string::npos);
  send("x\n"); assert(!watching && !bench.armed()); off();
  send("POLARITY HIGH\nWATCH\n"); poll();
  assert(Serial.output.find("raw=LOW U9=0") != std::string::npos);
  send("x\nARM\nSTEP 500\n");
  for (int i = 0; i < 20; ++i) poll();
  send("UNIT 0\n");  // Busy input aborts and discards, never changes selection.
  assert(selectedUnit == 9 && !bench.busy() && !bench.armed()); off();
  send("SPEED 1500\nARM\nSTEP 1500\n");
  for (int i = 0; i < 400; ++i) poll();
  assert(bench.busy() && bench.coils());
  send("ACCEL 2000\n"); off();
  assert(!bench.busy() && !bench.armed() && bench.config().acceleration == 400);
  send("UNIT 0\nSPEED 800\nACCEL 6000\nSET A\n"); off();
  assert(bench.manual() && !bench.homed() && !bench.armed() && bench.index() == 1);
  assert(Serial.output.find("reference=MANUAL value=A index=1") != std::string::npos);
  send("SHOW 5\n"); assert(!bench.busy()); off();
  send("ARM\nSHOW 5\r\n"); finish();
  assert(bench.manual() && bench.index() == 35);
  assert(bench.steps() == mega::Bench::positionFor(4096, 35) - mega::Bench::positionFor(4096, 1));
  assert(Serial.output.find("reference=MANUAL value=5 index=35") != std::string::npos);
  fakePins[mega::kHallPins[0]] = LOW;  // Manual wrapping ignores a permanently active Hall.
  send("SHOW B\n"); finish();
  assert(bench.index() == 2 && bench.manual());
  assert(bench.steps() == 4096 - mega::Bench::positionFor(4096, 35) + mega::Bench::positionFor(4096, 2));
  send("SHOW BLANK\n"); finish(); assert(bench.index() == 0 && bench.manual());
  send("set 9\narm\nshow 0\n"); finish(); assert(bench.index() == 30);
  send("SETINDEX 0\n"); off(); assert(bench.index() == 0 && !bench.armed());
  send("SET 0\n"); off(); assert(bench.index() == 30);  // Digit zero, not blank.
  send("SETINDEX 45\nSETINDEX 256\nSETINDEX 65536\nSET 10\nSET\nSHOW\nSHOW AB\n");
  assert(bench.index() == 30 && bench.manual() && !bench.busy()); off();
  send("SET AE\nARM\nSHOW UE\n"); finish(); assert(bench.index() == 29);
  send("FLAP\n"); finish(); assert(bench.index() == 30 && bench.manual());
  send("GOTO 44\n"); finish(); assert(bench.index() == 44);
  send("CYCLE\n"); finish(); assert(bench.index() == 44 && bench.steps() == 4096);
  send("SHOW A\n"); assert(bench.busy());
  send("SET 9\n"); off();  // Busy SET is discarded after immediate stop.
  assert(!bench.positioned() && !bench.armed() && bench.index() == 44);
  send("STATUS\n"); assert(Serial.output.find("reference=NONE value=UNKNOWN") != std::string::npos);
  send("SET BLANK\nSPEED 1000\n"); assert(!bench.positioned() && !bench.armed());
  send("SET BLANK\nUNIT 1\n"); assert(!bench.positioned() && !bench.armed());
  puts("PASS adapter: boot, CRLF, abort, parser limits, SPEED/ACCEL, configuration, isolation, WATCH, SET/SHOW values, manual wrap/cycle, invalidation");
}
