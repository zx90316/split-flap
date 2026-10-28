// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Arduino.h"
#include <cstring>

// BANK=0 register / ACK test double. Does not emulate bus waveforms, actual
// timeout behaviour, motor loads, voltage levels, BANK=1, or simultaneous power loss.
struct FakeWire {
  struct Write { uint8_t address, reg; uint16_t value; };
  uint8_t registers[4][32] = {};
  uint16_t hallRaw = 0x077f;
  uint8_t address = 0, cursor = 0;
  std::vector<uint8_t> tx;
  std::vector<Write> writes;
  uint32_t operationUs = 0;
  unsigned hallReads = 0, probes = 0;
  bool failAll = false, shortRead = false;
  int partialWriteAddress = -1, partialWriteRegister = -1;
  void begin(int, int, unsigned) {}
  void setTimeOut(unsigned) {}
  void beginTransmission(uint8_t value) { address = value; tx.clear(); }
  size_t write(uint8_t value) { tx.push_back(value); return 1; }
  uint8_t endTransmission(bool = true) {
    fakeNow += operationUs;
    if (tx.empty()) ++probes;
    if (failAll || address < 0x20 || address > 0x23 || fakeResetLevel == LOW) return 2;
    if (tx.empty()) return 0;
    cursor = tx[0];
    if (cursor >= 32 || tx.size() > 33u - cursor) return 3;
    if (tx.size() > 1) {
      const uint16_t value = static_cast<uint16_t>(tx[1]) |
          (tx.size() > 2 ? static_cast<uint16_t>(tx[2]) << 8 : 0);
      writes.push_back({address, cursor, value});
      if (address == partialWriteAddress && cursor == partialWriteRegister) {
        registers[address - 0x20][cursor] = tx[1];
        return 4;  // First data byte reached the chip before the transaction failed.
      }
    }
    for (size_t i = 1; i < tx.size(); ++i) registers[address - 0x20][cursor++] = tx[i];
    return 0;
  }
  size_t requestFrom(uint8_t value, size_t count, bool) {
    fakeNow += operationUs;
    address = value;
    if (address == 0x23 && cursor == 0x12) ++hallReads;
    if (failAll || fakeResetLevel == LOW || address < 0x20 || address > 0x23) return 0;
    return shortRead && count > 0 ? count - 1 : count;
  }
  int read() {
    if (address < 0x20 || address > 0x23 || cursor >= 32) return -1;
    if (address == 0x23 && cursor == 0x12) { ++cursor; return hallRaw & 0xff; }
    if (address == 0x23 && cursor == 0x13) { ++cursor; return hallRaw >> 8; }
    return registers[address - 0x20][cursor++];
  }
  void resetChips() {
    std::memset(registers, 0, sizeof(registers));
    for (auto& chip : registers) chip[0] = chip[1] = 0xff;
  }
};
extern FakeWire Wire;
