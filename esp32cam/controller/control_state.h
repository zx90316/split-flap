// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapLink.h>
namespace cam {
struct Job {
  uint32_t id = 0, request = 0;
  uint16_t hash = 0;
  uint8_t op = 0, stage = 0;  // 0 queued; otherwise sf::Stage
  sf::Error error = sf::Error::None;
};
struct State {
  bool hallEnabled = false, outputsDisabled = true, initialized = false, configStorageOk = true;
  uint8_t presentMask = 0, requiredMask = 1;
  uint32_t maxTickGapUs = 0, maxFlushUs = 0, lateTicks = 0;
  bool connected = false, valid = false, protocolMismatch = false, uncertain = false, maintenance = false;
  uint32_t boot = 0, session = 0, updatedAt = 0, nextRequest = 1;
  sf::PanelState panel;
  sf::UnitState units[10];
  sf::Settings settings;
  Job jobs[16];
};
// Wi-Fi provisioning also works before the motor hardware is connected.
// A disconnected snapshot can retain old busy flags; only local work still blocks it.
inline bool canConfigureNetwork(const State& s, bool linkPending, bool settingsPending) {
  return !linkPending && !settingsPending && !s.maintenance &&
      (!s.connected || !s.valid || (!s.panel.busy && !s.panel.saving && !s.panel.watching));
}
}
