// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapLink.h>
namespace cam {
// Contains SHA-256 digests only. Creating/removing a session is the only write.
class SessionTable {
 public:
  static const size_t kSize = 260;
  bool contains(const uint8_t* digest) const {
    uint8_t present = 0;
    for (const auto& entry : hashes_) {
      uint8_t different = 0, nonzero = 0;
      for (uint8_t i = 0; i < 32; ++i) { different |= entry[i] ^ digest[i]; nonzero |= entry[i]; }
      present |= different == 0 && nonzero != 0;
    }
    return present != 0;
  }
  void add(const uint8_t* digest) { memcpy(hashes_[next_], digest, 32); next_ = (next_ + 1) % 8; }
  void remove(const uint8_t* digest) { for (auto& h : hashes_) if (!memcmp(h, digest, 32)) memset(h, 0, 32); }
  void clear() { memset(hashes_, 0, sizeof(hashes_)); next_ = 0; }
  void encode(uint8_t* out) const { out[0] = 1; out[1] = next_; memcpy(out + 2, hashes_, 256); sf::put16(out + 258, sf::crc16(out, 258)); }
  bool decode(const uint8_t* in, size_t n) {
    if (n != kSize || in[0] != 1 || in[1] >= 8 || sf::get16(in + 258) != sf::crc16(in, 258)) return false;
    next_ = in[1]; memcpy(hashes_, in + 2, 256); return true;
  }
 private:
  uint8_t hashes_[8][32] = {}, next_ = 0;
};
}
