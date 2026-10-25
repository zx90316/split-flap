// SPDX-License-Identifier: GPL-3.0-only
#include "Arduino.h"
#include <assert.h>
#include <stdio.h>
uint32_t fakeNow = 0;
uint8_t fakePins[70] = {}, fakeModes[70] = {};
FakeSerial Serial;
#include "../unit_test/unit_test.ino"

void poll() {
  fakeNow += 1000; loop();
  uint8_t energized = 0;
  for (uint8_t u = 0; u < 10; ++u) {
    bool high = false;
    for (uint8_t p = 0; p < 4; ++p) high |= fakePins[mega::kMotorPins[u][p]] == HIGH;
    if (high) { assert(panel.enabled(u)); ++energized; }
  }
  assert(energized <= 1);  // Check the actual 40 simulated GPIO outputs on every loop.
}
void send(const std::string& text) {
  Serial.feed(text);
  do { poll(); } while (Serial.available());
}
void off() {
  for (int u = 0; u < 10; ++u)
    for (int p = 0; p < 4; ++p) assert(fakePins[mega::kMotorPins[u][p]] == LOW);
}

void finish() {
  for (uint32_t i = 0; panel.busy() && i < 2000000; ++i) poll();
  assert(!panel.busy() && !panel.error()); off();
}

int main() {
  setup(); off(); assert(!panel.selected().armed());
  assert(panel.digits() == 1 && panel.selectedUnit() == 0);
  send("UNIT 9\n"); assert(panel.selectedUnit() == 0); off();
  assert(Serial.output.find("SPEED [ALL] 800..30000") != std::string::npos);
  assert(Serial.output.find("ACCEL [ALL] 100..6000") != std::string::npos);
  send("SPEED 1500\nACCEL 600\nSTATUS\n"); off();
  assert(panel.selected().config().intervalUs == 1500 && panel.selected().config().acceleration == 600);
  assert(Serial.output.find("SPEED=1500 ACCEL=600") != std::string::npos);
  send("SPEED 799\nSPEED 30001\nACCEL 0\nACCEL 99\nACCEL 6001\nACCEL -1\nACCEL 65536\n");
  assert(panel.selected().config().intervalUs == 1500 && panel.selected().config().acceleration == 600);
  send("STEP 10\n"); off(); assert(!panel.selected().busy());
  send("ARM\r\nSTEP 100\r\n");
  assert(panel.selected().busy());  // Trailing CRLF must not abort a valid command.
  for (int i = 0; i < 20; ++i) poll();
  assert(panel.selected().coils());
  send("STOP\r\n"); off();
  assert(!panel.selected().busy() && !panel.selected().armed() && !panel.selected().homed());
  send("ARM\nSTEP 65536\n"); off(); assert(!panel.selected().busy());
  send("STEP -1\nSTEP 0\nGOTO 10\nORDER 1124\n"); off();
  assert(!panel.selected().busy());
  send(std::string(80, 'X') + "\n"); off(); assert(!panel.selected().armed());
  send("ORDER 1324\nDIR -1\nOFFSET 12\nREVSTEPS 4076\nSPEED 8000\n");
  assert(panel.selected().config().order[1] == 2 && panel.selected().config().direction == -1);
  assert(panel.selected().config().offset == 12 && panel.selected().config().revolution == 4076);
  assert(!panel.selected().armed());
  send("ARM\nACCEL 400\n"); off(); assert(!panel.selected().armed() && !panel.selected().homed());
  send("DIGITS 10\nUNIT 9\nARM\nSTEP 10\n");
  assert(panel.selected().config().intervalUs == 5000 && panel.selected().config().acceleration == 400);
  for (int i = 0; i < 1000 && panel.selected().busy(); ++i) {
    poll();
    for (int u = 0; u < 9; ++u)
      for (int p = 0; p < 4; ++p) assert(fakePins[mega::kMotorPins[u][p]] == LOW);
  }
  assert(!panel.selected().busy() && panel.selected().steps() == 10); off();
  send("WATCH\n"); assert(watching);
  fakePins[mega::kHallPins[9]] = LOW; poll();
  assert(Serial.output.find("raw=LOW U9=1") != std::string::npos);
  send("x\n"); assert(!watching && !panel.selected().armed()); off();
  send("POLARITY HIGH\nWATCH\n"); poll();
  assert(Serial.output.find("raw=LOW U9=0") != std::string::npos);
  send("x\nARM\nSTEP 500\n");
  for (int i = 0; i < 20; ++i) poll();
  send("UNIT 0\n");  // Busy input aborts and discards, never changes selection.
  assert(panel.selectedUnit() == 9 && !panel.selected().busy() && !panel.selected().armed()); off();
  send("SPEED 1500\nARM\nSTEP 1500\n");
  for (int i = 0; i < 400; ++i) poll();
  assert(panel.selected().busy() && panel.selected().coils());
  send("ACCEL 2000\n"); off();
  assert(!panel.selected().busy() && !panel.selected().armed() && panel.selected().config().acceleration == 400);
  send("UNIT 0\nREVSTEPS 4096\nOFFSET 0\nDIR 1\nORDER 1234\nPOLARITY LOW\nSPEED 800\nACCEL 6000\nSET A\n"); off();
  assert(panel.selected().manual() && !panel.selected().homed() && !panel.selected().armed() && panel.selected().index() == 1);
  assert(Serial.output.find("reference=MANUAL value=A index=1") != std::string::npos);
  send("SHOW 5\n"); assert(!panel.selected().busy()); off();
  send("ARM\nSHOW 5\r\n"); finish();
  assert(panel.selected().manual() && panel.selected().index() == 35);
  assert(panel.selected().steps() == mega::Bench::positionFor(4096, 35) - mega::Bench::positionFor(4096, 1));
  assert(Serial.output.find("reference=MANUAL value=5 index=35") != std::string::npos);
  fakePins[mega::kHallPins[0]] = LOW;  // Manual wrapping ignores a permanently active Hall.
  send("SHOW B\n"); finish();
  assert(panel.selected().index() == 2 && panel.selected().manual());
  assert(panel.selected().steps() == 4096 - mega::Bench::positionFor(4096, 35) + mega::Bench::positionFor(4096, 2));
  send("SHOW BLANK\n"); finish(); assert(panel.selected().index() == 0 && panel.selected().manual());
  send("set 9\narm\nshow 0\n"); finish(); assert(panel.selected().index() == 30);
  send("SETINDEX 0\n"); off(); assert(panel.selected().index() == 0 && !panel.selected().armed());
  send("SET 0\n"); off(); assert(panel.selected().index() == 30);  // Digit zero, not blank.
  send("SETINDEX 45\nSETINDEX 256\nSETINDEX 65536\nSET 10\nSET\nSHOW\nSHOW AB\n");
  assert(panel.selected().index() == 30 && panel.selected().manual() && !panel.selected().busy()); off();
  send("SET AE\nARM\nSHOW UE\n"); finish(); assert(panel.selected().index() == 29);
  send("FLAP\n"); finish(); assert(panel.selected().index() == 30 && panel.selected().manual());
  send("GOTO 44\n"); finish(); assert(panel.selected().index() == 44);
  send("CYCLE\n"); finish(); assert(panel.selected().index() == 44 && panel.selected().steps() == 4096);
  send("SHOW A\n"); assert(panel.selected().busy());
  send("SET 9\n"); off();  // Busy SET is discarded after immediate stop.
  assert(!panel.selected().positioned() && !panel.selected().armed() && panel.selected().index() == 44);
  send("STATUS\n"); assert(Serial.output.find("reference=NONE value=UNKNOWN") != std::string::npos);
  send("SET BLANK\nSPEED 1000\n"); assert(!panel.selected().positioned() && !panel.selected().armed());
  send("SET BLANK\nUNIT 1\n"); assert(!panel.selected().positioned() && !panel.selected().armed());
  for (uint8_t width = 1; width <= 10; ++width) {
    send("DIGITS " + std::to_string(width) + "\n"); off();
    assert(panel.digits() == width);
    send("SETTEXT " + std::string(width, '_') + "\nARM ALL\n"); off();
    std::string target;
    for (uint8_t i = 0; i < width; ++i) target += char('A' + i);
    send("TEXT " + target + "\r\n"); finish();
    for (uint8_t u = 0; u < width; ++u) assert(panel.unit(u).index() == u + 1);
    send("TEXT " + target + "\n"); assert(!panel.busy()); off();
  }
  send("DIGITS 0\nDIGITS 11\nDIGITS 65536\nUNITS 0,0\nUNITS 0,10\nUNITS 0,\n");
  assert(panel.digits() == 10); off();
  send("DIGITS 5\nSETTEXT _____\nARM ALL\nTEXT \"A B  \"\n"); finish();
  const uint8_t expected[] = {1, 0, 2, 0, 0};
  for (uint8_t u = 0; u < 5; ++u) assert(panel.unit(u).index() == expected[u]);
  send("TEXT LONGER\nTEXT AB/CD\nSETTEXT ABC\nSETTEXT ABC/!\nTEXT \"AB\n");
  assert(!panel.busy());
  for (uint8_t u = 0; u < 5; ++u) assert(panel.unit(u).index() == expected[u] && panel.unit(u).armed());
  send("TEXT \"\"\n"); finish();
  for (uint8_t u = 0; u < 5; ++u) assert(panel.unit(u).index() == 0);
  send("UNITS 9,0,4\nUNIT 1\nSETTEXT ABC\nARM ALL\nTEXT CBA\n"); finish();
  assert(panel.digits() == 3 && panel.physicalUnit(0) == 9 && panel.physicalUnit(1) == 0);
  assert(panel.unit(9).index() == 3 && panel.unit(0).index() == 2 && panel.unit(4).index() == 1);
  send("UNIT 9\nREVSTEPS 4076\nSPEED 800\nACCEL 800\nPOLARITY HIGH\nSET Z\nARM\nUNIT 0\n");
  assert(panel.selected().config().revolution == 4096);
  send("UNIT 9\nSTATUS\n");
  assert(panel.selected().config().revolution == 4076 && panel.selected().index() == 26 && panel.selected().armed());
  assert(!panel.activeLow(9) && panel.activeLow(0));
  send("DIGITS 2\nUNIT 9\n"); assert(panel.selectedUnit() == 0 && panel.digits() == 2);
  send("SPEED ALL 800\nACCEL ALL 800\n");
  for (uint8_t u = 0; u < 2; ++u)
    assert(panel.unit(u).config().intervalUs == 800 && panel.unit(u).config().acceleration == 800);
  send("SPEED ALL 799\nACCEL ALL 6001\nSPEED ALL\nSPEED ALL 800 EXTRA\n");
  assert(panel.unit(1).config().intervalUs == 800);
  send("DIGITS 3\nSETTEXT ___\nARM ALL\nTEXT ZZZ\n");
  for (uint8_t i = 0; i < 20; ++i) poll();
  send("DIGITS 10\n"); off();  // Busy layout changes cancel the whole batch, then are discarded.
  assert(!panel.busy() && panel.digits() == 3);
  const uint32_t stopped = panel.unit(0).steps();
  for (uint8_t i = 0; i < 100; ++i) poll();
  assert(panel.unit(0).steps() == stopped);
  for (uint8_t u = 0; u < 3; ++u) assert(!panel.unit(u).positioned() && !panel.unit(u).armed());
  send("ARM ALL\nTEXT ABC\n"); assert(!panel.busy()); off();
  send("SETTEXT ___\nARM ALL\nTEXT ABC\n"); finish();
  for (uint8_t u = 0; u < 3; ++u) assert(panel.unit(u).index() == u + 1);
  fakePins[mega::kHallPins[0]] = HIGH;  // No magnet / disconnected input with pull-up.
  send("UNIT 0\nREVSTEPS 1000\nARM ALL\nHOME ALL\n");
  for (uint32_t i = 0; panel.busy() && i < 20000; ++i) poll();
  assert(!panel.busy() && panel.error() && panel.faultUnit() == 0); off();
  assert(Serial.output.find("FAULT U0 hall_edge_missing") != std::string::npos);
  send("SETTEXT ABC\nARM ALL\nCYCLE ALL\n"); finish();
  for (uint8_t u = 0; u < 3; ++u) assert(panel.unit(u).index() == u + 1);
  send("STATUS ALL\n"); assert(Serial.output.find("UPDATE=SEQUENTIAL") != std::string::npos);
  puts("PASS adapter: single-unit compatibility; DIGITS 1..10; sparse UNITS; SETTEXT/TEXT spaces, padding, glyphs and lengths; independent state; batch readiness, stop and fault; GPIO isolation/sequential output");
}
