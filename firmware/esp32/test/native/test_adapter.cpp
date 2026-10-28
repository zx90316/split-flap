// SPDX-License-Identifier: GPL-3.0-only
// Exercise the actual Arduino adapter against logical doubles, without hardware.
// Does not establish real-time, electrical, brownout or physical motion safety.
#include <iostream>
#include <stdexcept>
#include <string>
#include "Arduino.h"
#include "Wire.h"

uint32_t fakeNow = 0;
int fakeResetLevel = HIGH;
std::vector<int> fakeGpioEvents;
FakeSerial Serial;
FakeWire Wire;
void fakeResetExpanders() { Wire.resetChips(); }

#include "../../src/main.cpp"

#define CHECK(expression) do { if (!(expression)) { \
  throw std::runtime_error(std::string(__FILE__) + ":" + \
      std::to_string(__LINE__) + ": " #expression); \
} } while (0)

namespace {
void command(const std::string& text) {
  std::vector<char> buffer(text.begin(), text.end()); buffer.push_back(0);
  executeCommand(buffer.data());
}
void freshBoot(const std::vector<unsigned char>& saved = {}) {
  motion = MotionController();
  settings = Preferences(); settings.bytes = saved;
  Serial = FakeSerial(); Wire = FakeWire();
  fakeNow = 0; fakeResetLevel = HIGH; fakeGpioEvents.clear();
  settingsAvailable = initialized = armed = noUmlauts = false;
  enabledCount = 0; hallMask = activeHighMask = 0;
  lastHallUs = lastAuditUs = lastLoopUs = 0;
  commandLength = 0; discardLine = false;
  // Model stale energized latches from an MCU-only restart, before setup.
  for (auto& chip : Wire.registers) chip[0x14] = chip[0x15] = 0xff;
  setup();
}
void stepLoop(uint32_t advance) { fakeNow += advance; loop(); }
void initEnabled(unsigned count) {
  command("INIT POWER_OFF"); CHECK(initialized);
  command("ENABLE " + std::to_string(count)); CHECK(enabledCount == count);
  command("ARM"); CHECK(armed); CHECK(!motion.busy());
}
void readyUnits(unsigned count) {
  initEnabled(count);
  command("LIMIT " + std::to_string(count));
  command("HOME ALL"); stepLoop(board::kHallPollUs); stepLoop(kHallDebounceUs); stepLoop(8000);
  const uint16_t packed = static_cast<uint16_t>((1u << count) - 1u);
  const uint16_t pins = (packed & 0x007f) | ((packed & 0x0380) << 1);
  Wire.hallRaw = board::kHallPinMask & ~pins;
  stepLoop(2000); stepLoop(kHallDebounceUs); stepLoop(kSettleUs);
  for (unsigned i = 0; i < count; ++i) {
    CHECK(motion.homed(i)); CHECK(motion.state(i) == State::Ready);
    CHECK(motion.coilMask(i) == 0);
  }
}
void assertFaultReleased() {
  CHECK(!initialized); CHECK(!armed); CHECK(!motion.busy());
  CHECK(fakeResetLevel == LOW);
  for (unsigned i = 0; i < 10; ++i) CHECK(motion.coilMask(i) == 0);
  for (const auto& chip : Wire.registers) {
    CHECK(chip[0] == 0xff && chip[1] == 0xff);
    CHECK(chip[0x14] == 0 && chip[0x15] == 0);
  }
}

void boot_init_arm_and_scan() {
  freshBoot();
  CHECK(fakeGpioEvents.size() >= 2);
  CHECK(fakeGpioEvents[0] == LOW && fakeGpioEvents[1] == 10 + OUTPUT_OPEN_DRAIN);
  CHECK(fakeResetLevel == LOW && !initialized && !armed && enabledCount == 0);
  CHECK(Wire.writes.empty()); CHECK(!motion.busy());
  command("ARM"); command("HOME ALL"); command("MOVE 0 2");
  CHECK(!armed && !motion.busy());
  command("ENABLE 1"); command("ARM"); CHECK(!armed);
  command("INIT POWER_OFF"); CHECK(initialized && !armed && !motion.busy());
  CHECK(fakeResetLevel == HIGH);
  CHECK(Wire.writes.size() == 12);
  for (unsigned chip = 0; chip < 4; ++chip) {
    CHECK(Wire.writes[chip * 3].reg == 0x14);
    CHECK(Wire.writes[chip * 3].value == 0);
    CHECK(Wire.writes[chip * 3 + 2].reg == 0);
  }
  command("ARM"); CHECK(armed && !motion.busy());
  command("SCAN"); CHECK(initialized && armed);
  Serial.clearOutput(); command("SCAN POWER_OFF");
  CHECK(Wire.probes == 8); CHECK(!initialized && !armed && !motion.busy());
  CHECK(fakeResetLevel == LOW);
  CHECK(Serial.output == "0x20\n0x21\n0x22\n0x23\n");
  command("ARM"); CHECK(!armed);
  command("INIT POWER_OFF"); CHECK(initialized && !armed);
}

void fresh_hall_after_slow_audit() {
  freshBoot(); initEnabled(1); command("HOME 0");
  motion.tick(100000, 0); motion.tick(105000, 0);
  CHECK(motion.state(0) == State::SeekMarker);
  fakeNow = 120000; lastLoopUs = fakeNow;
  lastHallUs = fakeNow - (board::kHallPollUs - 1);
  lastAuditUs = fakeNow - board::kBusAuditUs;
  hallMask = 0; Wire.hallRaw = 0x077e; Wire.operationUs = 350;
  const unsigned reads = Wire.hallReads;
  loop();
  CHECK(initialized); CHECK(Wire.hallReads == reads + 1);
  CHECK(hallMask == 1); CHECK(motion.coilMask(0) == 0);
  CHECK(motion.state(0) == State::SeekMarker);
}

void partial_write_and_short_read_release_all() {
  freshBoot(); initEnabled(5); command("LIMIT 5"); command("HOME ALL");
  Wire.partialWriteAddress = 0x21; Wire.partialWriteRegister = 0x14;
  Serial.clearOutput();
  for (unsigned i = 0; i < 20 && initialized; ++i) stepLoop(8000);
  assertFaultReleased();
  bool wroteFirstChip = false, attemptedSecondChip = false;
  for (const auto& write : Wire.writes) {
    if (write.reg == 0x14 && write.value && write.address == 0x20) wroteFirstChip = true;
    if (write.reg == 0x14 && write.value && write.address == 0x21) attemptedSecondChip = true;
  }
  CHECK(wroteFirstChip && attemptedSecondChip);
  for (int level : Serial.resetAtWrite) CHECK(level == LOW);
  CHECK(Serial.output.find("FAULT I2C_OUTPUT") != std::string::npos);
  freshBoot(); initEnabled(1); command("HOME 0"); stepLoop(0);
  Wire.shortRead = true; stepLoop(board::kHallPollUs);
  assertFaultReleased();
  CHECK(Serial.output.find("FAULT I2C_HALL") != std::string::npos);
}

void reset_detection_and_loop_stall() {
  freshBoot(); readyUnits(1); command("MOVE 0 4"); stepLoop(board::kHallPollUs); stepLoop(8000);
  CHECK(motion.coilMask(0) != 0);
  Wire.resetChips(); lastAuditUs = fakeNow - board::kBusAuditUs; stepLoop(0);
  assertFaultReleased(); CHECK(Serial.output.find("FAULT I2C_AUDIT") != std::string::npos);
  freshBoot(); initEnabled(1); command("HOME 0"); stepLoop(0);
  stepLoop(board::kMainLoopStallUs + 1);
  assertFaultReleased(); CHECK(Serial.output.find("FAULT LOOP_STALL") != std::string::npos);
}

void homing_fault_resets_before_uart_and_micros_rolls_over() {
  freshBoot(); initEnabled(1); command("CAL 0 1000 0 1 3000"); command("ARM");
  Wire.hallRaw = 0x077e; command("HOME 0"); Serial.clearOutput();
  for (unsigned i = 0; i < 2010 && initialized; ++i) stepLoop(3000);
  assertFaultReleased();
  CHECK(Serial.output.find("cause=home_step_limit") != std::string::npos);
  for (int level : Serial.resetAtWrite) CHECK(level == LOW);
  freshBoot(); initEnabled(1);
  fakeNow = UINT32_MAX - 10000; lastLoopUs = lastHallUs = lastAuditUs = fakeNow;
  command("HOME 0"); stepLoop(board::kHallPollUs); stepLoop(kHallDebounceUs); stepLoop(8000);
  CHECK(initialized && armed && fakeNow < 10000);
  CHECK(motion.state(0) == State::SeekMarker && motion.coilMask(0) != 0);
}

void stop_and_busy_gate() {
  freshBoot(); readyUnits(2); command("MOVE 0 3"); stepLoop(board::kHallPollUs); stepLoop(8000);
  CHECK(motion.busy() && motion.coilMask(0));
  const size_t writes = Wire.writes.size();
  command("CAL 0 1000 0 1 3000"); command("ENABLE 0"); command("SCAN POWER_OFF");
  CHECK(enabledCount == 2 && armed && initialized);
  CHECK(motion.config(0).stepsPerRevolution == 4096); CHECK(Wire.writes.size() == writes);
  command("STOP"); CHECK(!armed && !motion.busy());
  CHECK(!motion.homed(0) && !motion.homed(1));
  for (unsigned i = 0; i < 3; ++i) CHECK(Wire.registers[i][0x14] == 0 && Wire.registers[i][0x15] == 0);
  command("ARM"); command("MOVE 0 2"); CHECK(!motion.busy());
}

void text_requests_validate_before_queueing() {
  freshBoot(); readyUnits(2);
  const char* invalid[] = {"TEXT A?Z", "TEXT A@", "TEXT A\xc3", "TEXT A$"};
  for (const char* text : invalid) {
    command(text); CHECK(!motion.busy());
    CHECK(motion.targetIndex(0) == 0 && motion.targetIndex(1) == 0);
  }
  motion.stop(2); command("TEXT AB");
  CHECK(!motion.busy() && motion.targetIndex(0) == 0);
  freshBoot(); readyUnits(2); command("TEXT AZ");
  CHECK(motion.busy()); CHECK(motion.targetIndex(0) == 1 && motion.targetIndex(1) == 26);
}

void serial_long_line_is_wholly_discarded() {
  freshBoot(); initEnabled(1); command("STOP"); CHECK(!armed);
  Serial.feed("ARM" + std::string(200, ' ') + "\nARM\n");
  while (Serial.available() && Serial.output.find("ERR line too long") == std::string::npos) {
    const int before = Serial.available(); pollSerial(); CHECK(before - Serial.available() <= 32);
    CHECK(!armed);
  }
  CHECK(Serial.output.find("ERR line too long; discarded") != std::string::npos);
  CHECK(!armed && commandLength == 0 && !discardLine);
  pollSerial(); CHECK(armed && !motion.busy());
}

StoredConfig validStored() {
  StoredConfig stored{}; stored.version = kStorageVersion;
  for (unsigned i = 0; i < 10; ++i) {
    stored.revolution[i] = 4000 + i; stored.offset[i] = i;
    stored.interval[i] = 8000 + i; stored.direction[i] = i % 2 ? -1 : 1;
  }
  stored.polarity = 0x201; stored.noUmlauts = 1; return stored;
}
std::vector<unsigned char> bytesOf(const StoredConfig& stored) {
  const auto* first = reinterpret_cast<const unsigned char*>(&stored);
  return std::vector<unsigned char>(first, first + sizeof(stored));
}
void nvs_is_validated_as_one_configuration() {
  auto stored = validStored(); freshBoot(bytesOf(stored));
  CHECK(activeHighMask == 0x201 && noUmlauts && !armed && enabledCount == 0);
  for (unsigned i = 0; i < 10; ++i) {
    CHECK(motion.config(i).stepsPerRevolution == stored.revolution[i]);
    CHECK(!motion.config(i).enabled && !motion.homed(i));
  }
  // Invalid late entries must not leave earlier units partly loaded.
  stored.direction[9] = 0; freshBoot(bytesOf(stored));
  CHECK(activeHighMask == 0 && !noUmlauts);
  for (unsigned i = 0; i < 10; ++i) CHECK(motion.config(i).stepsPerRevolution == 4096);
  stored = validStored(); stored.offset[9] = stored.revolution[9]; freshBoot(bytesOf(stored));
  CHECK(motion.config(0).stepsPerRevolution == 4096 && activeHighMask == 0);
  stored = validStored(); stored.version = 99; freshBoot(bytesOf(stored));
  CHECK(motion.config(0).stepsPerRevolution == 4096);
  stored = validStored(); stored.polarity = 0x400; freshBoot(bytesOf(stored));
  CHECK(motion.config(0).stepsPerRevolution == 4096 && activeHighMask == 0);
  freshBoot(std::vector<unsigned char>(3, 0)); CHECK(motion.config(0).stepsPerRevolution == 4096);
  initEnabled(1); command("CAL 0 4076 17 -1 9000"); command("POLARITY 0 HIGH");
  command("CHARSET NOUM"); command("SAVE"); CHECK(settings.bytes.size() == sizeof(StoredConfig));
  const auto saved = settings.bytes; freshBoot(saved);
  CHECK(motion.config(0).stepsPerRevolution == 4076 && motion.config(0).homeOffsetSteps == 17);
  CHECK(motion.config(0).phaseDirection == -1 && motion.config(0).halfStepIntervalUs == 9000);
  CHECK(activeHighMask == 1 && noUmlauts && !armed && enabledCount == 0);
  CHECK(!motion.config(0).enabled);
  settings.writeFails = true; CHECK(!saveSettings());
}
}  // namespace

int main() {
  try {
    boot_init_arm_and_scan(); fresh_hall_after_slow_audit();
    partial_write_and_short_read_release_all(); reset_detection_and_loop_stall();
    homing_fault_resets_before_uart_and_micros_rolls_over();
    stop_and_busy_gate(); text_requests_validate_before_queueing();
    serial_long_line_is_wholly_discarded(); nvs_is_validated_as_one_configuration();
    std::cout << "Adapter boot/reset, arming, scan, fresh Hall, partial I2C failures, stall, stop, serial atomicity and NVS tests passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
