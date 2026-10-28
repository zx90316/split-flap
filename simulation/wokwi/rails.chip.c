// SPDX-License-Identifier: GPL-3.0-only
// DIGITAL reference rails. Labels describe intended real domains, not simulated volts.
#include "wokwi-api.h"
void chip_init(void) {
  pin_init("V5_LOGIC",OUTPUT_HIGH);
  pin_init("V12_MOTOR",OUTPUT_HIGH);
  pin_init("GND",OUTPUT_LOW);
}
