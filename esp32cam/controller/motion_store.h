// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapLink.h>

namespace cam {
// Two independent atomic NVS blobs, with generation, format and CRC validation.
template <class Backend> class MotionStore {
 public:
  static const size_t kSize = 132;
  explicit MotionStore(Backend& backend) : io_(backend) {}
  bool healthy() const { return io_.healthy(); }
  bool load(sf::Settings& out, bool& hall) {
    bool found = false; uint32_t seq = 0; sf::Settings candidate; bool candidateHall = false;
    for (uint8_t slot = 0; slot < 2; ++slot) {
      uint8_t bytes[kSize];
      if (!io_.read(slot, bytes, sizeof(bytes)) || !decode(bytes, candidate, candidateHall, seq)) continue;
      if (!found || int32_t(seq - generation_) > 0) {
        found = true; out = candidate; hall = candidateHall; generation_ = seq; active_ = slot;
      }
    }
    valid_ = found; return found;
  }
  bool save(const sf::Settings& s, bool hall) {
    if (!healthy() || !sf::validSettings(s)) return false;
    uint8_t bytes[kSize]; memcpy(bytes, "SFM2", 4); sf::put32(bytes + 4, generation_ + 1);
    sf::encodeSettings(s, bytes + 8); bytes[129] = hall; sf::put16(bytes + 130, sf::crc16(bytes, 130));
    const uint8_t target = valid_ ? 1 - active_ : 0;
    if (!io_.write(target, bytes, sizeof(bytes))) return false;
    uint8_t check[kSize]; sf::Settings verified; bool vHall = false; uint32_t seq = 0;
    if (!io_.read(target, check, sizeof(check)) || memcmp(check, bytes, sizeof(check)) || !decode(check, verified, vHall, seq)) return false;
    active_ = target; generation_ = seq; valid_ = true; return true;
  }
 private:
  static bool decode(const uint8_t* bytes, sf::Settings& s, bool& hall, uint32_t& seq) {
    if (memcmp(bytes, "SFM2", 4) || bytes[129] > 1 || sf::get16(bytes + 130) != sf::crc16(bytes, 130) ||
        !sf::decodeSettings(bytes + 8, sf::kSettingsSize, s)) return false;
    hall = bytes[129] != 0; seq = sf::get32(bytes + 4); return true;
  }
  Backend& io_; uint32_t generation_ = 0; uint8_t active_ = 0; bool valid_ = false;
};
}
