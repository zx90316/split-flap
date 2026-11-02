// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
class Preferences {
 public:
  static std::map<std::string, std::vector<uint8_t>>& records() { static std::map<std::string, std::vector<uint8_t>> data; return data; }
  static bool& failWrites() { static bool fail = false; return fail; }
  bool begin(const char*, bool) { return true; }
  size_t getBytesLength(const char* key) { return records()[key].size(); }
  size_t getBytes(const char* key, void* out, size_t n) {
    const auto& r = records()[key]; if (r.size() != n) return 0; memcpy(out, r.data(), n); return n;
  }
  size_t putBytes(const char* key, const void* data, size_t n) {
    if (failWrites()) return 0;
    const auto* b = static_cast<const uint8_t*>(data); records()[key] = std::vector<uint8_t>(b, b + n); return n;
  }
};
