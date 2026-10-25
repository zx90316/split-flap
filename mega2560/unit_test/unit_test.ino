// SPDX-License-Identifier: GPL-3.0-only
// Mega 2560, ONE selected unit at a time. See ../README.md before powering motors.
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include "bench.h"
#include "pins.h"

#if defined(ARDUINO) && !defined(__AVR_ATmega2560__)
#error Select Arduino Mega or Mega 2560 (ATmega2560).
#endif
#ifdef __AVR__
#include <avr/wdt.h>
// Disable a watchdog left running after reset, before Arduino initialization.
void disableBootWatchdog() __attribute__((naked, section(".init3"), used));
void disableBootWatchdog() { MCUSR = 0; wdt_disable(); }
#endif

mega::Bench bench;
uint8_t selectedUnit = 0, lastOutput = 255, reportedMeasures = 0;
bool activeLow = true, watching = false, discardLine = false;
char inputLine[64];
uint8_t inputLength = 0;
uint32_t watchAt = 0;
int lastRaw = -1;

void allOutputsLow() {
  for (uint8_t u = 0; u < 10; ++u)
    for (uint8_t p = 0; p < 4; ++p) digitalWrite(mega::kMotorPins[u][p], LOW);
  lastOutput = 0;
}

void writeOutputs() {
  const uint8_t mask = bench.coils();
  if (mask == lastOutput) return;
  // Break before make; GPIOs only drive ULN inputs, never motor windings.
  for (uint8_t p = 0; p < 4; ++p) digitalWrite(mega::kMotorPins[selectedUnit][p], LOW);
  for (uint8_t p = 0; p < 4; ++p)
    if (mask & (1 << p)) digitalWrite(mega::kMotorPins[selectedUnit][p], HIGH);
  lastOutput = mask;
}

bool number(const char* text, uint16_t& value) {
  if (!text || !*text) return false;
  uint32_t result = 0;
  while (*text) {
    if (*text < '0' || *text > '9') return false;
    result = result * 10 + (*text++ - '0');
    if (result > 65535) return false;
  }
  value = static_cast<uint16_t>(result);
  return true;
}

void status() {
  const mega::Config& c = bench.config();
  Serial.print(F("U")); Serial.print(selectedUnit);
  Serial.print(F(" armed=")); Serial.print(bench.armed());
  Serial.print(F(" homed=")); Serial.print(bench.homed());
  Serial.print(F(" index=")); Serial.print(bench.index());
  Serial.print(F(" REVSTEPS=")); Serial.print(c.revolution);
  Serial.print(F(" OFFSET=")); Serial.print(c.offset);
  Serial.print(F(" SPEED=")); Serial.print(c.intervalUs);
  Serial.print(F(" DIR=")); Serial.print(c.direction);
  Serial.print(F(" ORDER="));
  for (uint8_t i = 0; i < 4; ++i) Serial.print(c.order[i] + 1);
  Serial.print(F(" POLARITY=")); Serial.print(activeLow ? F("LOW") : F("HIGH"));
  Serial.print(F(" error=")); Serial.println(bench.error() ? bench.error() : "none");
}

void help() {
  Serial.println(F("Mega bench: 115200, newline. Motors OFF at boot. No EEPROM writes."));
  Serial.println(F("STATUS | WATCH | STOP | OFF | ARM | UNIT 0..9"));
  Serial.println(F("LED (UNPLUG MOTOR) | STEP 1..20000 | REV | HOME | MEASURE"));
  Serial.println(F("FLAP | GOTO 0..44 | CYCLE (HOME required for these three)"));
  Serial.println(F("ORDER 1234 | DIR 1/-1 | REVSTEPS 1000..20000 | OFFSET 0..rev-1"));
  Serial.println(F("SPEED 3000..30000 (us/half-step) | POLARITY LOW/HIGH"));
  Serial.println(F("Settings disarm and invalidate position. ARM again before motion."));
  Serial.println(F("Any non-newline byte during motion/WATCH aborts the ENTIRE line."));
  Serial.println(F("STOP cannot cut 12V or protect against controller/power failure."));
}

void command(char* line) {
  char* cmd = strtok(line, " \t");
  if (!cmd) return;
  char* arg = strtok(nullptr, " \t");
  char* extra = strtok(nullptr, " \t");
  if (extra) { Serial.println(F("ERR too many arguments")); return; }
  uint16_t n = 0;
  const bool numeric = number(arg, n);
  const uint32_t now = micros();
  bool ok = false;
  if (!strcmp(cmd, "UNIT") && numeric && n < 10) {
    bench.stop(); allOutputsLow(); selectedUnit = n;
    bench = mega::Bench(); activeLow = true;
    Serial.println(F("OK selected unit; defaults restored, disarmed")); status(); return;
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
  else if (!strcmp(cmd, "POLARITY") && arg && (!strcmp(arg, "LOW") || !strcmp(arg, "HIGH"))) {
    bench.stop(); allOutputsLow(); activeLow = !strcmp(arg, "LOW");
    // Reset the debouncer when the electrical interpretation changes.
    bench = mega::Bench(); bench.configure(c);
    Serial.println(F("OK polarity; disarmed")); return;
  } else setting = false;
  if (setting) {
    ok = bench.configure(c); writeOutputs();
    Serial.println(ok ? F("OK settings; disarmed, HOME required") : F("ERR invalid settings")); return;
  }
  if (arg && strcmp(cmd, "STEP") && strcmp(cmd, "GOTO")) {
    Serial.println(F("ERR unknown command or invalid argument")); return;
  }
  if (!strcmp(cmd, "HELP") && !arg) { help(); return; }
  if (!strcmp(cmd, "STATUS") && !arg) { status(); return; }
  if ((!strcmp(cmd, "STOP") || !strcmp(cmd, "OFF")) && !arg) {
    bench.stop(); allOutputsLow(); Serial.println(F("OK stopped; disarmed, position invalid")); return;
  }
  if (!strcmp(cmd, "WATCH") && !arg) {
    watching = true; lastRaw = -1; watchAt = now;
    Serial.println(F("WATCH: raw HIGH/LOW and Ux=active; any key + newline exits")); return;
  }
  if (!strcmp(cmd, "ARM") && !arg) { bench.arm(); Serial.println(F("OK armed; physical 12V switch remains required")); return; }
  if (!strcmp(cmd, "LED") && !arg) ok = bench.led(now);
  else if (!strcmp(cmd, "STEP") && numeric) ok = bench.step(n, now);
  else if (!strcmp(cmd, "REV") && !arg) ok = bench.step(c.revolution, now);
  else if (!strcmp(cmd, "HOME") && !arg) ok = bench.home(now);
  else if (!strcmp(cmd, "MEASURE") && !arg) ok = bench.measure(now);
  else if (!strcmp(cmd, "FLAP") && !arg) ok = bench.goTo((bench.index() + 1) % 45, now);
  else if (!strcmp(cmd, "GOTO") && numeric && n <= 44) ok = bench.goTo(n, now);
  else if (!strcmp(cmd, "CYCLE") && !arg) ok = bench.cycle(now);
  if (ok) reportedMeasures = 0;
  writeOutputs();
  Serial.println(ok ? F("BUSY (any non-newline byte stops)") : F("ERR command/range; ARM and, for display, HOME first"));
}

void setup() {
  // Initialize ALL 40 allocated phase outputs low before accepting commands.
  for (uint8_t u = 0; u < 10; ++u) {
    for (uint8_t p = 0; p < 4; ++p) {
      digitalWrite(mega::kMotorPins[u][p], LOW);
      pinMode(mega::kMotorPins[u][p], OUTPUT);
    }
    pinMode(mega::kHallPins[u], INPUT_PULLUP);
  }
  allOutputsLow();
  Serial.begin(115200);
  help();
#ifdef __AVR__
  wdt_enable(WDTO_1S);
#endif
}

void loop() {
#ifdef __AVR__
  wdt_reset();
#endif
  // Bound input handling so a serial flood cannot starve motion scheduling.
  for (uint8_t i = 0; i < 64 && Serial.available(); ++i) {
    const char ch = Serial.read();
    const bool eol = ch == '\r' || ch == '\n';
    if (discardLine) { if (eol) discardLine = false; continue; }
    if (bench.busy() || watching) {
      if (eol) continue;  // Consume a command's trailing CRLF without aborting.
      bench.stop(); watching = false; allOutputsLow();
      inputLength = 0; discardLine = true;
      Serial.println(F("STOPPED; line discarded, disarmed, position invalid"));
      continue;
    }
    if (eol) {
      inputLine[inputLength] = 0; inputLength = 0; command(inputLine);
    } else if (ch < 32 || ch > 126 || inputLength == sizeof(inputLine) - 1) {
      inputLength = 0; discardLine = true;
      bench.stop(); allOutputsLow(); Serial.println(F("ERR invalid/long line; discarded, disarmed"));
    } else inputLine[inputLength++] = toupper(static_cast<unsigned char>(ch));
  }
  const uint32_t now = micros();
  const int raw = digitalRead(mega::kHallPins[selectedUnit]);
  const bool wasBusy = bench.busy();
  bench.tick(now, activeLow ? raw == LOW : raw == HIGH);
  writeOutputs();
  if (bench.measuredCount() > reportedMeasures) {
    Serial.print(F("MEASURE ")); Serial.print(reportedMeasures + 1);
    Serial.print(F(" halfsteps=")); Serial.println(bench.measured(reportedMeasures++));
  }
  if (wasBusy && !bench.busy()) {
    Serial.print(bench.error() ? F("FAULT ") : F("DONE "));
    if (bench.error()) Serial.println(bench.error());
    else Serial.println(F("coils off"));
    status();
  }
  if (watching && (raw != lastRaw || static_cast<uint32_t>(now - watchAt) >= 1000000)) {
    lastRaw = raw; watchAt = now;
    Serial.print(F("A")); Serial.print(selectedUnit);
    Serial.print(F(" raw=")); Serial.print(raw == LOW ? F("LOW") : F("HIGH"));
    Serial.print(F(" U")); Serial.print(selectedUnit); Serial.print('=');
    Serial.println(activeLow ? raw == LOW : raw == HIGH);
  }
}
