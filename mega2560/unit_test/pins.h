// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>

namespace mega {
// Physical connector order, IN1..IN4. U0 is the leftmost display unit.
// D22..D53 are a double-row header: consecutive pins in a row differ by 2.
static const uint8_t kMotorPins[10][4] = {
    {22, 24, 26, 28}, {30, 32, 34, 36}, {38, 40, 42, 44},
    {46, 48, 50, 52}, {23, 25, 27, 29}, {31, 33, 35, 37},
    {39, 41, 43, 45}, {47, 49, 51, 53}, {2, 3, 4, 5}, {8, 9, 10, 11}};
// Arduino Mega digital aliases for A0..A9; no other board is supported.
static const uint8_t kHallPins[10] = {54, 55, 56, 57, 58, 59, 60, 61, 62, 63};
}
