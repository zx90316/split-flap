// SPDX-License-Identifier: GPL-3.0-only
#include "Arduino.h"
#include "remote.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
uint32_t fakeNow = 0;
uint8_t fakePins[70] = {}, fakeModes[70] = {};
FakeSerial Serial, Serial1;
struct Memory {
  uint8_t bytes[4096];
  Memory() { memset(bytes, 0xff, sizeof(bytes)); }
  uint8_t read(uint16_t a) const { return bytes[a]; }
  void update(uint16_t a, uint8_t b) { bytes[a] = b; }
};
void codecs() {
  assert(sf::crc16(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0x29b1);
  uint32_t random = 23;
  for (int round = 0; round < 4000; ++round) {
    sf::Packet p; p.kind = sf::Kind::Command; p.session = random; p.id = round;
    p.length = round % (sf::kMaxPayload + 1);
    for (uint8_t i = 0; i < p.length; ++i) { random = random * 1664525 + 1013904223; p.data[i] = uint8_t(random >> 24); }
    uint8_t wire[sf::kWireSize]; const size_t n = sf::encode(p, wire); assert(n <= sizeof(wire));
    sf::Decoder d; sf::Packet q; bool got = false;
    for (size_t i = 0; i < n; ++i) { const bool complete = d.feed(wire[i], uint32_t(i / 8), q); if (complete) got = true; }
    assert(got && p.session == q.session && p.id == q.id && p.length == q.length && !memcmp(p.data, q.data, p.length));
    wire[n - 2] ^= 0x11; sf::Decoder bad; got = false;
    for (size_t i = 0; i < n; ++i) got |= bad.feed(wire[i], 0, q);
    assert(!got);
  }
  sf::Packet p, q; p.kind = sf::Kind::Ping; uint8_t wire[sf::kWireSize]; const size_t n = sf::encode(p, wire);
  sf::Decoder d;
  for (int i = 0; i < 600; ++i) assert(!d.feed(8, 0, q));
  assert(!d.feed(0, 0, q));
  bool ok = false; for (size_t i = 0; i < n; ++i) ok |= d.feed(wire[i], 1, q); assert(ok);
  sf::Decoder expired; expired.feed(wire[0], 0, q);
  for (size_t i = 1; i < n; ++i) assert(!expired.feed(wire[i], 200, q));
  ok = false; for (size_t i = 0; i < n; ++i) ok |= expired.feed(wire[i], 201, q); assert(ok);
  sf::Settings s; uint8_t bytes[sf::kSettingsSize];
  for (uint8_t count = 1; count <= 10; ++count) {
    s.count = count;
    for (uint8_t i = 0; i < count; ++i) s.mapping[i] = 9 - i;
    sf::encodeSettings(s, bytes); sf::Settings q2; assert(sf::decodeSettings(bytes, sizeof(bytes), q2));
    assert(q2.count == count && q2.mapping[0] == 9);
  }
  bytes[1] = bytes[2]; sf::Settings rejected; assert(!sf::decodeSettings(bytes, sizeof(bytes), rejected));
  puts("PASS COBS/CRC fragmentation, noise, overflow, timeout and settings codecs (4000 packets)");
}
void storage() {
  Memory original; mega::SettingsStore<Memory> initial(original); sf::Settings first;
  first.units[0].speed = 800; first.units[0].accel = 800;
  assert(initial.begin(first)); while (initial.busy()) initial.tick(); assert(!initial.failed());
  sf::Settings second = first; second.count = 3; second.mapping[0] = 7; second.mapping[1] = 0; second.mapping[2] = 2;
  second.units[7].direction = -1;
  for (int cut = 0; cut <= 134; ++cut) {
    Memory interrupted = original; mega::SettingsStore<Memory> writer(interrupted); sf::Settings loaded;
    assert(writer.load(loaded)); assert(writer.begin(second));
    for (int tick = 0; tick < cut && writer.busy(); ++tick) writer.tick();
    mega::SettingsStore<Memory> boot(interrupted); assert(boot.load(loaded));
    assert(loaded.count == (cut >= 134 ? 3 : 1));
    assert(loaded.units[0].speed == 800 && loaded.units[0].accel == 800);
  }
  mega::SettingsStore<Memory> writer(original); sf::Settings loaded; assert(writer.load(loaded));
  assert(writer.begin(second)); while (writer.busy()) writer.tick();
  original.bytes[160 + 12] ^= 0xff;
  mega::SettingsStore<Memory> recover(original); assert(recover.load(loaded) && loaded.count == 1);
  sf::Settings invalid = first; invalid.units[9].order[0] = 2;
  assert(!writer.begin(invalid));
  Memory legacy; sf::Settings old;
  for (auto& c : old.units) { c.speed = 5000; c.accel = 400; }
  old.units[7].speed = 1500; old.units[7].accel = 600; old.units[7].revolution = 4076;
  memcpy(legacy.bytes, "SFMC", 4); sf::put32(legacy.bytes + 4, 1); legacy.bytes[8] = 1;
  sf::encodeSettings(old, legacy.bytes + 9); sf::put16(legacy.bytes + 130, sf::crc16(legacy.bytes, 130)); legacy.bytes[159] = 0xa5;
  for (int cut = 0; cut <= 134; ++cut) {
    Memory interrupted = legacy; mega::SettingsStore<Memory> migrate(interrupted);
    assert(migrate.load(loaded) && migrate.needsMigration());
    assert(loaded.units[0].speed == 800 && loaded.units[0].accel == 800);
    assert(loaded.units[7].speed == 1500 && loaded.units[7].accel == 600 && loaded.units[7].revolution == 4076);
    assert(migrate.begin(loaded)); for (int i = 0; i < cut && migrate.busy(); ++i) migrate.tick();
    mega::SettingsStore<Memory> restart(interrupted); assert(restart.load(loaded));
    assert(restart.needsMigration() == (cut < 134));
    assert(loaded.units[0].speed == 800 && loaded.units[7].speed == 1500);
  }
  puts("PASS EEPROM cuts at every write, corrupt-slot fallback and complete validation");
  puts("PASS v1 default-speed migration to 800/800, custom calibration retained, power cuts at every migration write");
}
struct Harness {
  mega::Panel panel; Memory mem; mega::Remote<Memory> remote; FakeSerial uart;
  uint32_t us = 0, session = 0x12345678;
  sf::Decoder response; std::vector<sf::Packet> packets;
  Harness() : remote(panel, mem) { remote.begin(); send(sf::Kind::Hello, 0); run(180); }
  void send(sf::Kind kind, uint32_t id, const std::vector<uint8_t>& data = {}) {
    sf::Packet p; p.kind = kind; p.session = session; p.id = id; p.length = uint8_t(data.size());
    assert(data.size() <= sf::kMaxPayload); if (!data.empty()) memcpy(p.data, data.data(), data.size());
    uint8_t wire[sf::kWireSize]; const size_t n = sf::encode(p, wire);
    uart.feed(std::string(reinterpret_cast<char*>(wire), n));
  }
  void command(uint32_t id, sf::Op op, uint8_t unit, std::vector<uint8_t> args = {}) {
    args.insert(args.begin(), unit); args.insert(args.begin(), uint8_t(op)); send(sf::Kind::Command, id, args); run(25);
  }
  void run(int ms, bool heartbeat = true) {
    for (int i = 0; i < ms; ++i) {
      us += 1000;
      if (heartbeat && us / 1000 % 500 == 0) send(sf::Kind::Ping, 0);
      panel.tick(us, 0x3ff); remote.poll(uart, us / 1000, us, 0x3ff);
      uint8_t on = 0;
      for (uint8_t u = 0; u < 10; ++u) if (panel.unit(u).coils()) { assert(panel.enabled(u)); ++on; }
      assert(on <= panel.digits());
      for (char ch : uart.output) { sf::Packet p; if (response.feed(uint8_t(ch), us / 1000, p)) packets.push_back(p); }
      uart.output.clear();
    }
  }
  bool result(uint32_t id, sf::Stage stage, sf::Error error = sf::Error::None) const {
    for (const auto& p : packets) if (p.kind == sf::Kind::Result && p.id == id && p.data[0] == uint8_t(stage) && p.data[1] == uint8_t(error)) return true;
    return false;
  }
};
void remote() {
  Harness h; sf::Settings settings; settings.count = 3; settings.mapping[0] = 0; settings.mapping[1] = 2; settings.mapping[2] = 7;
  for (auto& c : settings.units) { c.speed = 800; c.accel = 800; }
  uint8_t bytes[sf::kSettingsSize]; sf::encodeSettings(settings, bytes);
  h.command(1, sf::Op::ApplyConfig, sf::kAll, std::vector<uint8_t>(bytes, bytes + sizeof(bytes))); h.run(250);
  assert(h.result(1, sf::Stage::Done) && h.panel.digits() == 3 && !h.panel.unit(7).armed());
  h.command(2, sf::Op::SetText, sf::kAll, {3, 1, 2, 3});
  h.command(3, sf::Op::Arm, sf::kAll);
  h.command(4, sf::Op::Text, sf::kAll, {3, 2, 3, 4});
  assert(h.panel.busy());
  h.send(sf::Kind::Query, 0); h.run(100); assert(h.panel.busy());
  const uint32_t steps = h.panel.unit(0).steps();
  h.command(4, sf::Op::Text, sf::kAll, {3, 2, 3, 4});
  assert(h.panel.unit(0).steps() >= steps && h.panel.busy());
  h.command(5, sf::Op::Speed, 0, {0x20, 0x03}); assert(h.result(5, sf::Stage::Rejected, sf::Error::Busy));
  h.command(6, sf::Op::Stop, sf::kAll); h.run(40);
  assert(!h.panel.busy() && !h.panel.unit(0).positioned() && !h.panel.unit(7).armed());
  assert(h.result(4, sf::Stage::Fault, sf::Error::Stopped) && h.result(6, sf::Stage::Done));
  h.command(7, sf::Op::Arm, sf::kAll); h.command(8, sf::Op::Step, 0, {0x20, 0x4e});
  h.run(2200, false); assert(!h.panel.busy() && !h.remote.owned() && !h.panel.unit(0).armed());
  assert(h.result(8, sf::Stage::Fault, sf::Error::LinkLost));
  h.command(9, sf::Op::Arm, sf::kAll); assert(h.result(9, sf::Stage::Rejected, sf::Error::Session));
  h.command(10, sf::Op::Claim, sf::kAll); h.command(11, sf::Op::Arm, sf::kAll);
  h.command(12, sf::Op::Step, 0, {100, 0});
  h.remote.usbActivity(); h.panel.stopAll(); h.run(30);
  h.command(13, sf::Op::Arm, sf::kAll); assert(h.result(13, sf::Stage::Rejected, sf::Error::UsbOverride));
  h.command(14, sf::Op::Claim, sf::kAll); assert(!h.remote.locked());
  h.command(15, sf::Op::SetText, sf::kAll, {3, 0, 0, 0}); h.command(16, sf::Op::Arm, sf::kAll);
  h.command(17, sf::Op::Text, sf::kAll, {3, 1, 1, 1});
  h.session = 123; h.send(sf::Kind::Hello, 0); h.run(50);
  assert(!h.panel.busy() && !h.panel.unit(0).positioned() && !h.panel.unit(7).armed());
  // Retired requests older than the replay cache are rejected, never executed again.
  for (uint32_t id = 1; id <= 12; ++id) h.command(id, sf::Op::Select, 0);
  h.command(1, sf::Op::Arm, sf::kAll); assert(h.result(1, sf::Stage::Rejected, sf::Error::Unknown));
  assert(!h.panel.unit(0).armed());
  mega::SettingsStore<Memory> reboot(h.mem); sf::Settings restored; assert(reboot.load(restored));
  mega::Panel fresh; assert(fresh.applySettings(restored));
  assert(fresh.digits() == 3 && fresh.unit(7).config().intervalUs == 800 && !fresh.unit(7).armed() && !fresh.unit(7).positioned());
  puts("PASS remote nonblocking status, deduplication, busy/STOP, lease loss, USB takeover, reconnect and persisted reboot");
}
int main() { codecs(); storage(); remote(); }
