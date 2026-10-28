// SPDX-License-Identifier: GPL-3.0-only
// Mega 2560, explicitly enabled units; changed digits begin moving together.
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include "panel.h"
#include "pins.h"
#include <EEPROM.h>
#include "remote.h"

#if defined(ARDUINO) && !defined(__AVR_ATmega2560__)
#error Select Arduino Mega or Mega 2560 (ATmega2560).
#endif
#ifdef __AVR__
#include <avr/wdt.h>
void disableBootWatchdog() __attribute__((naked, section(".init3"), used));
void disableBootWatchdog() { MCUSR = 0; wdt_disable(); }
#endif

mega::Panel panel;
struct EepromBackend {
  uint8_t read(uint16_t address) { return EEPROM.read(address); }
  void update(uint16_t address, uint8_t value) { EEPROM.update(address, value); }
} eepromBackend;
mega::Remote<EepromBackend> remote(panel, eepromBackend);
uint8_t lastOutput[mega::kMaxUnits] = {}, reportedMeasures[mega::kMaxUnits] = {};
bool watching = false, discardLine = false;
char inputLine[64];
uint8_t inputLength = 0;
uint32_t watchAt = 0;
int lastRaw = -1;

void allOutputsLow() {
  for (uint8_t u = 0; u < mega::kMaxUnits; ++u) {
    for (uint8_t p = 0; p < 4; ++p) digitalWrite(mega::kMotorPins[u][p], LOW);
    lastOutput[u] = 0;
  }
}

void writeOutputs() {
  for (uint8_t u = 0; u < mega::kMaxUnits; ++u) {
    const uint8_t mask = panel.enabled(u) ? panel.unit(u).coils() : 0;
    if (mask == lastOutput[u]) continue;
    // Break before make. Disabled units never receive a HIGH phase output.
    for (uint8_t p = 0; p < 4; ++p) digitalWrite(mega::kMotorPins[u][p], LOW);
    for (uint8_t p = 0; p < 4; ++p)
      if (mask & (1 << p)) digitalWrite(mega::kMotorPins[u][p], HIGH);
    lastOutput[u] = mask;
  }
}

bool number(const char* text, uint16_t& value) {
  if (!text || !*text) return false;
  uint32_t result = 0;
  while (*text) {
    if (*text < '0' || *text > '9') return false;
    result = result * 10 + (*text++ - '0');
    if (result > 65535) return false;
  }
  value = static_cast<uint16_t>(result); return true;
}

bool unitList(const char* text, uint8_t* units, uint8_t& count) {
  count = 0;
  if (!text || !*text) return false;
  while (*text) {
    if (count == mega::kMaxUnits || *text < '0' || *text > '9') return false;
    units[count++] = *text++ - '0';
    if (!*text) return true;
    if (*text++ != ',' || !*text) return false;
  }
  return false;
}

void printValue(uint8_t index) {
  if (index == 0) Serial.print(F("BLANK"));
  else if (index == 27) Serial.print(F("AE"));
  else if (index == 28) Serial.print(F("OE"));
  else if (index == 29) Serial.print(F("UE"));
  else Serial.print(mega::characterFor(index));
}

void panelStatus() {
  Serial.print(F("DIGITS=")); Serial.print(panel.digits());
  Serial.print(F(" UNITS="));
  for (uint8_t i = 0; i < panel.digits(); ++i) {
    if (i) Serial.print(',');
    Serial.print(panel.physicalUnit(i));
  }
  Serial.print(F(" SELECTED=U")); Serial.print(panel.selectedUnit());
  Serial.println(F(" UPDATE=PARALLEL"));
}

void statusUnit(uint8_t unit) {
  const mega::Bench& bench = panel.unit(unit);
  const mega::Config& c = bench.config();
  Serial.print(F("U")); Serial.print(unit);
  Serial.print(F(" armed=")); Serial.print(bench.armed());
  Serial.print(F(" homed=")); Serial.print(bench.homed());
  Serial.print(F(" reference="));
  Serial.print(bench.manual() ? F("MANUAL") : bench.homed() ? F("HALL") : F("NONE"));
  Serial.print(F(" value="));
  if (bench.positioned()) printValue(bench.index()); else Serial.print(F("UNKNOWN"));
  Serial.print(F(" index=")); Serial.print(bench.index());
  Serial.print(F(" REVSTEPS=")); Serial.print(c.revolution);
  Serial.print(F(" OFFSET=")); Serial.print(c.offset);
  Serial.print(F(" SPEED=")); Serial.print(c.intervalUs);
  Serial.print(F(" ACCEL=")); Serial.print(c.acceleration);
  Serial.print(F(" DIR=")); Serial.print(c.direction);
  Serial.print(F(" ORDER="));
  for (uint8_t i = 0; i < 4; ++i) Serial.print(c.order[i] + 1);
  Serial.print(F(" POLARITY=")); Serial.print(panel.activeLow(unit) ? F("LOW") : F("HIGH"));
  const char* error = panel.faultUnit() == unit ? panel.error() : bench.error();
  Serial.print(F(" error=")); Serial.println(error ? error : "none");
}

void status() { panelStatus(); statusUnit(panel.selectedUnit()); }
void statusAll() {
  panelStatus();
  for (uint8_t i = 0; i < panel.digits(); ++i) statusUnit(panel.physicalUnit(i));
}

void help() {
  Serial.println(F("Mega panel + ESP32 link v1: 115200. Motors OFF; saved settings load at boot."));
  Serial.println(F("SAVE | LOAD (EEPROM layout/calibration only; never position or ARM)"));
  Serial.println(F("DIGITS 1..10 (U0 onward) | UNITS 0,2,4 (explicit ordered installation map)"));
  Serial.println(F("UNIT 0..9 (enabled only; keeps settings/position) | STATUS [ALL]"));
  Serial.println(F("ARM [ALL] | STOP | OFF | WATCH | HELP"));
  Serial.println(F("SETTEXT exact-current-text | TEXT target-text (pads blanks, parallel)"));
  Serial.println(F("Bulk text: changed digits start together; _=blank, $/&/#=AE/OE/UE"));
  Serial.println(F("SET value | SHOW value | SETINDEX 0..44 (selected unit)"));
  Serial.println(F("Values: BLANK, A..Z, AE/OE/UE ($/&/#), 0..9, : . - ? !"));
  Serial.println(F("LED (UNPLUG MOTOR) | STEP 1..20000 | REV | HOME [ALL] | MEASURE"));
  Serial.println(F("FLAP | GOTO 0..44 | CYCLE [ALL] (ARM + position from SET or HOME required)"));
  Serial.println(F("ORDER 1234 | DIR 1/-1 | REVSTEPS 1000..20000 | OFFSET 0..rev-1"));
  Serial.println(F("SPEED [ALL] 800..30000 (cruise us/half-step) | ACCEL [ALL] 100..6000 (half-steps/s^2)"));
  Serial.println(F("Ramped moves start/end at 10000us or slower; HOME/MEASURE use 5000us or slower."));
  Serial.println(F("POLARITY LOW/HIGH | SPEED/ACCEL preserve position/ARM; geometry changes need SET/ARM."));
  Serial.println(F("No automatic plug detection. Only explicitly enabled units may move."));
  Serial.println(F("Any non-newline byte during motion/WATCH stops ALL and discards the line."));
  Serial.println(F("STOP cannot cut 12V or protect against controller/power failure."));
}

void reportStart(bool ok, bool all) {
  writeOutputs();
  if (!ok) Serial.println(F("ERR command/range/readiness; enable units, SET current or HOME, and ARM first"));
  else if (panel.busy()) Serial.println(F("BUSY (any non-newline byte stops ALL)"));
  else {
    Serial.println(F("DONE unchanged; coils off"));
    if (all) statusAll(); else status();
  }
}

void command(char* line) {
  // Preserve bulk-text payloads before ordinary token parsing.
  char* cmd = line;
  while (*cmd == ' ' || *cmd == '\t') ++cmd;
  if (!*cmd) return;
  const bool readOnly = !strncmp(cmd, "STATUS", 6) || !strncmp(cmd, "HELP", 4);
  const bool stopping = !strcmp(cmd, "STOP") || !strcmp(cmd, "OFF");
  if (remote.saving() && !readOnly && !stopping) { Serial.println(F("ERR EEPROM saving; wait")); return; }
  if (!readOnly) remote.usbActivity();
  if (!strcmp(cmd, "SAVE")) { Serial.println(remote.saveLocal() ? F("OK saving EEPROM") : F("ERR save busy")); return; }
  if (!strcmp(cmd, "LOAD")) { Serial.println(remote.loadLocal() ? F("OK loaded; disarmed, positions invalid") : F("ERR no valid saved config/busy")); return; }
  char* payload = cmd;
  while (*payload && *payload != ' ' && *payload != '\t') ++payload;
  if (*payload) *payload++ = 0;
  while (*payload == ' ' || *payload == '\t') ++payload;
  const uint32_t now = micros();
  if (!strcmp(cmd, "SETTEXT") || !strcmp(cmd, "TEXT")) {
    char* end = payload + strlen(payload);
    while (end > payload && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    if (*payload == '"') {
      if (end - payload < 2 || end[-1] != '"') { Serial.println(F("ERR text quotes")); return; }
      ++payload; end[-1] = 0;
    }
    uint8_t values[mega::kMaxUnits];
    const bool declaring = !strcmp(cmd, "SETTEXT");
    if (!mega::parseText(payload, panel.digits(), declaring, values)) {
      Serial.println(F("ERR text glyph/length; SETTEXT needs exactly DIGITS, TEXT at most DIGITS")); return;
    }
    if (declaring) {
      const bool ok = panel.setCurrentText(values, panel.digits()); writeOutputs();
      Serial.println(ok ? F("OK manual references; no motion, disarmed; ARM ALL before TEXT") : F("ERR busy"));
      if (ok) statusAll();
    } else reportStart(panel.text(values, panel.digits(), now), true);
    return;
  }
  char* arg = strtok(payload, " \t");
  char* extra = strtok(nullptr, " \t");
  char* fourth = strtok(nullptr, " \t");
  const bool all = arg && !strcmp(arg, "ALL");
  const bool allMotionSetting = all && (!strcmp(cmd, "SPEED") || !strcmp(cmd, "ACCEL"));
  if (fourth || (extra && !allMotionSetting)) { Serial.println(F("ERR too many arguments")); return; }
  uint16_t n = 0;
  const bool numeric = number(arg, n);
  bool ok = false;
  if (!strcmp(cmd, "DIGITS") || !strcmp(cmd, "UNITS")) {
    if (!strcmp(cmd, "DIGITS")) ok = numeric && n >= 1 && n <= mega::kMaxUnits && panel.setDigits(n);
    else {
      uint8_t units[mega::kMaxUnits], count = 0;
      ok = unitList(arg, units, count) && panel.setUnits(units, count);
    }
    if (ok) allOutputsLow();
    Serial.println(ok ? F("OK installation map; all disarmed, positions invalid") : F("ERR installation map"));
    if (ok) panelStatus();
    return;
  }
  if (!strcmp(cmd, "UNIT")) {
    ok = numeric && n < mega::kMaxUnits && panel.select(n);
    Serial.println(ok ? F("OK selected enabled unit; settings and position retained") : F("ERR UNIT disabled or invalid; use DIGITS/UNITS first"));
    if (ok) status(); return;
  }
  if (!strcmp(cmd, "STATUS") && (!arg || all)) { if (all) statusAll(); else status(); return; }
  if (allMotionSetting) {
    ok = number(extra, n) && (!strcmp(cmd, "SPEED") ? panel.speedAll(n) : panel.accelerationAll(n));
    if (ok) panel.clearError();
    writeOutputs();
    Serial.println(ok ? F("OK active-unit settings; disarmed, SETTEXT or HOME ALL required") : F("ERR invalid settings")); return;
  }
  if (!strcmp(cmd, "ARM") && all) {
    ok = panel.armAll();
    Serial.println(ok ? F("OK active units armed; physical 12V switch remains required") : F("ERR busy")); return;
  }
  if ((!strcmp(cmd, "HOME") || !strcmp(cmd, "CYCLE")) && all) {
    reportStart(!strcmp(cmd, "HOME") ? panel.homeAll(now) : panel.cycleAll(now), true); return;
  }
  mega::Bench& bench = panel.selected();
  if (!strcmp(cmd, "SET") || !strcmp(cmd, "SETINDEX")) {
    uint8_t index = 0;
    const bool valid = !strcmp(cmd, "SET") ? mega::indexFor(arg, index) : numeric && n < mega::kFlapCount;
    if (!strcmp(cmd, "SETINDEX")) index = static_cast<uint8_t>(n);
    ok = valid && bench.setCurrent(index);
    if (ok) panel.clearError();
    writeOutputs();
    Serial.println(ok ? F("OK manual reference; no motion, disarmed; ARM before SHOW") : F("ERR current value or index"));
    if (ok) status(); return;
  }
  mega::Config c = bench.config();
  bool setting = true;
  if (!strcmp(cmd, "ORDER") && arg && strlen(arg) == 4) {
    for (uint8_t i = 0; i < 4; ++i) c.order[i] = arg[i] - '1';
  } else if (!strcmp(cmd, "DIR") && arg && (!strcmp(arg, "1") || !strcmp(arg, "-1"))) {
    c.direction = !strcmp(arg, "1") ? 1 : -1;
  } else if (!strcmp(cmd, "REVSTEPS") && numeric) c.revolution = n;
  else if (!strcmp(cmd, "OFFSET") && numeric) c.offset = n;
  else if (!strcmp(cmd, "SPEED") && numeric) c.intervalUs = n;
  else if (!strcmp(cmd, "ACCEL") && numeric) c.acceleration = n;
  else if (!strcmp(cmd, "POLARITY") && arg && (!strcmp(arg, "LOW") || !strcmp(arg, "HIGH"))) {
    ok = panel.setPolarity(!strcmp(arg, "LOW"));
    if (ok) panel.clearError();
    writeOutputs(); Serial.println(ok ? F("OK polarity; disarmed") : F("ERR busy")); return;
  } else setting = false;
  if (setting) {
    ok = bench.configure(c);
    if (ok) panel.clearError();
    writeOutputs();
    Serial.println(ok ? F("OK settings; disarmed, SET current or HOME required") : F("ERR invalid settings")); return;
  }
  if (arg && strcmp(cmd, "STEP") && strcmp(cmd, "GOTO") && strcmp(cmd, "SHOW")) {
    Serial.println(F("ERR unknown command or invalid argument")); return;
  }
  if (!strcmp(cmd, "HELP") && !arg) { help(); return; }
  if ((!strcmp(cmd, "STOP") || !strcmp(cmd, "OFF")) && !arg) {
    panel.stopAll(); allOutputsLow(); Serial.println(F("OK ALL stopped; disarmed, positions invalid")); return;
  }
  if (!strcmp(cmd, "WATCH") && !arg) {
    watching = true; lastRaw = -1; watchAt = now;
    Serial.println(F("WATCH: selected Hall raw HIGH/LOW; any key + newline exits and stops ALL")); return;
  }
  if (!strcmp(cmd, "ARM") && !arg) {
    ok = bench.arm(); if (ok) panel.clearError();
    Serial.println(ok ? F("OK armed; physical 12V switch remains required") : F("ERR busy")); return;
  }
  if (!strcmp(cmd, "LED") && !arg) ok = bench.led(now);
  else if (!strcmp(cmd, "STEP") && numeric) ok = bench.step(n, now);
  else if (!strcmp(cmd, "REV") && !arg) ok = bench.step(c.revolution, now);
  else if (!strcmp(cmd, "HOME") && !arg) ok = bench.home(now);
  else if (!strcmp(cmd, "MEASURE") && !arg) ok = bench.measure(now);
  else if (!strcmp(cmd, "FLAP") && !arg) ok = bench.goTo((bench.index() + 1) % mega::kFlapCount, now);
  else if (!strcmp(cmd, "GOTO") && numeric && n < mega::kFlapCount) ok = bench.goTo(n, now);
  else if (!strcmp(cmd, "SHOW")) {
    uint8_t index = 0;
    if (!mega::indexFor(arg, index)) { Serial.println(F("ERR target value")); return; }
    ok = bench.goTo(index, now);
  } else if (!strcmp(cmd, "CYCLE") && !arg) ok = bench.cycle(now);
  if (ok) { reportedMeasures[panel.selectedUnit()] = 0; panel.clearError(); }
  reportStart(ok, false);
}

void setup() {
  for (uint8_t u = 0; u < mega::kMaxUnits; ++u) {
    for (uint8_t p = 0; p < 4; ++p) {
      digitalWrite(mega::kMotorPins[u][p], LOW);
      pinMode(mega::kMotorPins[u][p], OUTPUT);
    }
    pinMode(mega::kHallPins[u], INPUT_PULLUP);
  }
  allOutputsLow(); Serial.begin(115200); Serial1.begin(115200); remote.begin(); help(); panelStatus();
#ifdef __AVR__
  wdt_enable(WDTO_1S);
#endif
}

void loop() {
#ifdef __AVR__
  wdt_reset();
#endif
  for (uint8_t i = 0; i < 64 && Serial.available(); ++i) {
    const char ch = Serial.read();
    const bool eol = ch == '\r' || ch == '\n';
    if (discardLine) { if (eol) discardLine = false; continue; }
    if (panel.busy() || watching) {
      if (eol) continue;
      panel.stopAll(); watching = false; allOutputsLow();
      remote.usbActivity();
      inputLength = 0; discardLine = true;
      Serial.println(F("STOPPED ALL; line discarded, disarmed, positions invalid")); continue;
    }
    if (eol) { inputLine[inputLength] = 0; inputLength = 0; command(inputLine); }
    else if (ch < 32 || ch > 126 || inputLength == sizeof(inputLine) - 1) {
      inputLength = 0; discardLine = true; panel.stopAll(); allOutputsLow();
      Serial.println(F("ERR invalid/long line; discarded, ALL disarmed"));
    } else inputLine[inputLength++] = toupper(static_cast<unsigned char>(ch));
  }
  const uint32_t now = micros();
  uint16_t highMask = 0;
  for (uint8_t i = 0; i < panel.digits(); ++i) {
    const uint8_t u = panel.physicalUnit(i);
    if (digitalRead(mega::kHallPins[u]) == HIGH) highMask |= 1U << u;
  }
  const bool wasBusy = panel.busy(), wasBatch = panel.batching();
  panel.tick(now, highMask);
  remote.poll(Serial1, millis(), micros(), highMask, watching);
  if (remote.consumeStopWatch()) watching = false;
  writeOutputs();
  const uint8_t u = panel.selectedUnit();
  mega::Bench& bench = panel.selected();
  if (!remote.owned() && bench.measuredCount() > reportedMeasures[u]) {
    Serial.print(F("MEASURE U")); Serial.print(u);
    Serial.print(' '); Serial.print(reportedMeasures[u] + 1);
    Serial.print(F(" halfsteps=")); Serial.println(bench.measured(reportedMeasures[u]++));
  }
  if (wasBusy && !panel.busy() && !remote.owned()) {
    if (panel.error()) {
      Serial.print(F("FAULT U")); Serial.print(panel.faultUnit()); Serial.print(' '); Serial.println(panel.error());
    } else Serial.println(F("DONE coils off"));
    if (wasBatch) statusAll(); else status();
  }
  const int raw = (highMask & (1U << u)) ? HIGH : LOW;
  if (watching && (raw != lastRaw || static_cast<uint32_t>(now - watchAt) >= 1000000)) {
    lastRaw = raw; watchAt = now;
    Serial.print(F("A")); Serial.print(u);
    Serial.print(F(" raw=")); Serial.print(raw == LOW ? F("LOW") : F("HIGH"));
    Serial.print(F(" U")); Serial.print(u); Serial.print('=');
    Serial.println(panel.activeLow(u) ? raw == LOW : raw == HIGH);
  }
}
