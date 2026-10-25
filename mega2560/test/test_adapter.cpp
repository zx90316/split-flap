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

int main() {
  setup(); off(); assert(!bench.armed());
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
  send("UNIT 9\nARM\nSTEP 10\n");
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
  puts("PASS adapter: boot, CRLF, abort, parser limits, configuration, isolation, WATCH");
}
