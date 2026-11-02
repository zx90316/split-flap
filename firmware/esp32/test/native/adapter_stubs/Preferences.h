// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>
#include <cstring>
#include <vector>

// Single-key byte store double. Real NVS persistence/power-failure guarantees
// must be provided and tested by ESP32 Preferences/NVS, not this host test.
struct Preferences {
  std::vector<unsigned char> bytes;
  bool available = true, writeFails = false;
  bool begin(const char*, bool) { return available; }
  size_t getBytesLength(const char*) { return bytes.size(); }
  size_t getBytes(const char*, void* output, size_t size) {
    if (size != bytes.size()) return 0;
    std::memcpy(output, bytes.data(), size); return size;
  }
  size_t putBytes(const char*, const void* source, size_t size) {
    if (writeFails) return 0;
    const auto* data = static_cast<const unsigned char*>(source);
    bytes.assign(data, data + size); return size;
  }
};
