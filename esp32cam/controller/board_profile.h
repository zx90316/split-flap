// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>

// This firmware is for the purchased classic ESP32 / ESP-WROOM-32 board.
// Select ESP32 Dev Module, 4MB, min_spiffs, PSRAM disabled (see tools/dev.py).
#if defined(ARDUINO) && (!defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32))
#error This controller requires a classic ESP32 (NodeMCU ESP-32S), not an ESP32-C3/S3.
#endif
#if defined(ARDUINO_ESP32_CAM) || defined(BOARD_HAS_PSRAM)
#error Select the NodeMCU ESP-32S build workflow with PSRAM disabled.
#endif

namespace cam {
#ifdef SPLITFLAP_SINGLE
// Single controller: ESP32 drives the ULN2003 inputs and reads Hall through four MCP23017.
// A different OTA project keeps a dual-controller image off this wiring, and vice versa.
static const bool kSingleController = true;
static const char kHardwareName[] = "NodeMCU ESP-32S + MCP23017";
static const char kFirmwareVersion[] = "2.0.0";
static const char kOtaProject[] = "splitflap-32s-s1";  // Sixteen bytes, no trailing NUL on wire.
static const uint8_t kMcpSda = 21, kMcpScl = 22, kMcpReset = 23;
#define SF_PEER "馬達控制"
#else
static const bool kSingleController = false;
static const char kHardwareName[] = "NodeMCU ESP-32S";
static const char kFirmwareVersion[] = "1.3.1";
static const char kOtaProject[] = "splitflap-32s-v1";  // Sixteen bytes, no trailing NUL on wire.
static const uint8_t kUartRx = 14, kUartTx = 13;
#define SF_PEER "Mega"
#endif
static const char kOtaMagic[] = "SFESP01";  // Eight bytes including the trailing NUL.
}
