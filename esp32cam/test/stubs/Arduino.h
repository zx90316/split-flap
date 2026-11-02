// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../../../mega2560/test/stubs/Arduino.h"
inline uint32_t esp_random() { static uint32_t seed = 42; seed = seed * 1664525U + 1013904223U; return seed; }
