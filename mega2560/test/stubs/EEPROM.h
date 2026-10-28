#pragma once
#include <stdint.h>
#include <string.h>
struct FakeEEPROM {
  uint8_t bytes[4096];
  FakeEEPROM() { memset(bytes, 0xff, sizeof(bytes)); }
  uint8_t read(uint16_t address) const { return bytes[address]; }
  void update(uint16_t address, uint8_t value) { bytes[address] = value; }
};
static FakeEEPROM EEPROM;
