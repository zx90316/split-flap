// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <SplitFlapLink.h>

namespace mega {
// Two 160-byte slots. Commit marker is written last; the previous slot is never erased.
template <typename Backend> class SettingsStore {
 public:
  static const uint16_t kSlotSize = 160, kRecordSize = 132;
  explicit SettingsStore(Backend& backend) : backend_(backend) {}
  bool load(sf::Settings& out) {
    sf::Settings candidate; uint32_t seq = 0; uint8_t version = 0; bool found = false;
    for (uint8_t slot = 0; slot < 2; ++slot) {
      if (readSlot(slot, candidate, seq, version) && (!found || int32_t(seq - sequence_) > 0)) {
        out = candidate; sequence_ = seq; active_ = slot; activeVersion_ = version; found = true;
      }
    }
    valid_ = found; migration_ = found && activeVersion_ == 1;
    if (migration_) for (auto& c : out.units) {
      if (c.speed == 5000 && c.accel == 400) { c.speed = 800; c.accel = 800; }
    }
    return found;
  }
  bool begin(const sf::Settings& s) {
    if (busy_ || !sf::validSettings(s)) return false;
    memcpy(record_, "SFMC", 4); sf::put32(record_ + 4, sequence_ + 1); record_[8] = 2;
    sf::encodeSettings(s, record_ + 9); sf::put16(record_ + 130, sf::crc16(record_, 130));
    target_ = valid_ ? uint8_t(1 - active_) : 0; cursor_ = 0; busy_ = true; failed_ = false; return true;
  }
  void tick() {
    if (!busy_) return;
    const uint16_t base = uint16_t(target_) * kSlotSize;
    if (cursor_ == 0) backend_.update(base + kSlotSize - 1, 0);
    else if (cursor_ <= kRecordSize) backend_.update(base + cursor_ - 1, record_[cursor_ - 1]);
    else {
      backend_.update(base + kSlotSize - 1, 0xa5);
      sf::Settings check; uint32_t seq = 0; uint8_t version = 0;
      failed_ = !readSlot(target_, check, seq, version) || seq != sequence_ + 1 || version != 2;
      if (!failed_) { active_ = target_; sequence_ = seq; valid_ = true; activeVersion_ = 2; migration_ = false; }
      busy_ = false;
    }
    ++cursor_;
  }
  bool busy() const { return busy_; }
  bool failed() const { return failed_; }
  bool valid() const { return valid_; }
  bool needsMigration() const { return migration_; }
  uint32_t sequence() const { return sequence_; }
 private:
  bool readSlot(uint8_t slot, sf::Settings& out, uint32_t& seq, uint8_t& version) {
    const uint16_t base = uint16_t(slot) * kSlotSize;
    if (backend_.read(base + kSlotSize - 1) != 0xa5) return false;
    uint8_t bytes[kRecordSize];
    for (uint16_t i = 0; i < kRecordSize; ++i) bytes[i] = backend_.read(base + i);
    if (memcmp(bytes, "SFMC", 4) || (bytes[8] != 1 && bytes[8] != 2) || sf::get16(bytes + 130) != sf::crc16(bytes, 130) ||
        !sf::decodeSettings(bytes + 9, sf::kSettingsSize, out)) return false;
    seq = sf::get32(bytes + 4); version = bytes[8]; return true;
  }
  Backend& backend_;
  uint8_t record_[kRecordSize] = {}, active_ = 0, target_ = 0, activeVersion_ = 2;
  uint16_t cursor_ = 0;
  uint32_t sequence_ = 0;
  bool busy_ = false, failed_ = false, valid_ = false, migration_ = false;
};
}
