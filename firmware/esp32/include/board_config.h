// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
namespace board {
constexpr uint8_t kSda = 21, kScl = 22, kReset = 23;
constexpr uint32_t kI2cHz = 100000;
constexpr uint16_t kHallPinMask = 0x077f; // GPA0-6 and GPB0-2 only.
constexpr uint8_t kMotorAddresses[3] = {0x20, 0x21, 0x22};
constexpr uint8_t kHallAddress = 0x23;
constexpr uint32_t kHallPollUs = 2000;
constexpr uint32_t kBusAuditUs = 250000;
constexpr uint32_t kMainLoopStallUs = 100000;
constexpr uint16_t kAllUnits = 0x03ff;
}
