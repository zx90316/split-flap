// SPDX-License-Identifier: GPL-3.0-only
#include "../../esp32cam/controller/mcp_bank.h"
#include "../../esp32cam/controller/motion_store.h"
#include "../../esp32cam/controller/local_engine.h"
#include <assert.h>
#include <stdio.h>
#include <vector>

struct FakeWire {
  uint64_t us = 0;
  uint8_t present = 15;
  bool held = true, nack = false;
  uint16_t registers[4][0x16] = {};
  uint16_t physical[10] = {};
  uint32_t steps[10] = {}, edges[10] = {};
  uint8_t previous[10] = {}, energizedPeak = 0;
  uint16_t hallRaw = 0x077f;
  bool automaticHall = false;
  uint32_t hallReads = 0;
  uint32_t nowUs() const { return uint32_t(us); }
  uint32_t nowMs() const { return uint32_t(us / 1000); }
  void delayUs(uint32_t n) { us += n; }
  void reset(bool hold) {
    held = hold;
    if (hold) { memset(registers, 0, sizeof(registers)); memset(previous, 0, sizeof(previous));
      for (auto& r : registers) r[0] = 0xffff; }
  }
  bool probe(uint8_t a) { us += 25; return !held && (present & (1 << (a - 0x20))); }
  bool write16(uint8_t a, uint8_t r, uint16_t value) {
    us += 100;
    if (nack || !probe(a)) return false;
    const uint8_t chip = a - 0x20; registers[chip][r] = value;
    if (r == 0x14 && chip < 3) for (uint8_t n = 0; n < 4 && chip * 4 + n < 10; ++n) {
      const uint8_t u = chip * 4 + n, mask = (value >> (n * 4)) & 15;
      if (mask && mask != previous[u]) {
        if (edges[u]) assert(uint32_t(nowUs() - edges[u]) >= 800);
        edges[u] = nowUs(); ++steps[u]; physical[u] = (physical[u] + 1) % 4096;
      }
      previous[u] = mask;
    }
    uint8_t energized = 0; for (uint8_t v : previous) energized += v != 0;
    if (energized > energizedPeak) energizedPeak = energized;
    return true;
  }
  bool read8(uint8_t a, uint8_t r, uint8_t& v) { us += 100; if (nack || !probe(a)) return false; v = registers[a - 0x20][r]; return true; }
  bool read16(uint8_t a, uint8_t r, uint16_t& v) {
    us += 125; if (nack || !probe(a)) return false;
    if (a == 0x23 && r == 0x12) {
      v = hallRaw;
      if (automaticHall) for (uint8_t u = 0; u < 10; ++u) if (physical[u] < 16) v &= ~(1U << (u < 7 ? u : u + 1));
      ++hallReads;
    } else v = registers[a - 0x20][r];
    return true;
  }
};
struct FakeNvs {
  uint8_t slots[2][132] = {};
  bool exists[2] = {}, ok = true;
  size_t cut = 132;
  bool healthy() const { return ok; }
  bool read(uint8_t slot, uint8_t* data, size_t n) { if (!exists[slot]) return false; memcpy(data, slots[slot], n); return true; }
  bool write(uint8_t slot, const uint8_t* data, size_t n) {
    if (!ok) return false;
    memcpy(slots[slot], data, cut < n ? cut : n); exists[slot] = true; return cut >= n;
  }
};
using Bank = cam::McpBank<FakeWire>;
using Store = cam::MotionStore<FakeNvs>;
struct System {
  FakeWire wire; FakeNvs nvs; Bank bank; Store store; cam::LocalEngine<Bank, Store> engine;
  System() : bank(wire), store(nvs), engine(bank, store) { engine.begin(123); }
  void run(uint32_t duration) {
    const uint64_t end = wire.us + duration;
    while (wire.us < end) { wire.us += 250; engine.tick(); }
  }
  cam::Job job(uint32_t id) const { for (const auto& j : engine.state().jobs) if (j.id == id) return j; assert(false); return {}; }
  uint32_t send(sf::Op op, uint8_t unit = sf::kAll, std::initializer_list<uint8_t> args = {}) {
    uint8_t b[160] = {uint8_t(op), unit}; size_t n = 2; for (uint8_t a : args) b[n++] = a;
    uint32_t id = 0; assert(engine.submit(b, n, engine.state().nextRequest, id) == sf::Error::None); run(2000); return id;
  }
  void done(uint32_t id, uint32_t limit = 15000000) {
    for (uint32_t t = 0; job(id).stage < 2 && t < limit; t += 10000) run(10000);
    assert(job(id).stage == uint8_t(sf::Stage::Done));
  }
  uint32_t config(const sf::Settings& s, bool hall = false) {
    uint8_t b[sf::kSettingsSize + 3] = {uint8_t(sf::Op::ApplyConfig), sf::kAll};
    sf::encodeSettings(s, b + 2); b[sizeof(b) - 1] = hall; uint32_t id = 0;
    assert(engine.submit(b, sizeof(b), engine.state().nextRequest, id) == sf::Error::None); run(3000); return id;
  }
  uint32_t text(sf::Op op, const uint8_t* values, uint8_t count) {
    uint8_t b[13] = {uint8_t(op), sf::kAll, count}; memcpy(b + 3, values, count); uint32_t id = 0;
    assert(engine.submit(b, count + 3, engine.state().nextRequest, id) == sf::Error::None); run(2000); return id;
  }
};
void widths() {
  uint32_t singleTime = 0;
  for (uint8_t count = 1; count <= 10; ++count) {
    System s; sf::Settings settings; settings.count = count; for (uint8_t u = 0; u < count; ++u) settings.mapping[u] = u;
    s.done(s.config(settings)); uint8_t values[10] = {}, targets[10]; memset(targets, 26, sizeof(targets));
    s.done(s.text(sf::Op::SetText, values, count)); s.done(s.send(sf::Op::Arm));
    const uint32_t start = s.wire.nowUs(), job = s.text(sf::Op::Text, targets, count);
    s.run(50000); for (uint8_t u = 0; u < count; ++u) assert(s.wire.steps[u] > 0);
    s.done(job); const uint32_t elapsed = s.wire.nowUs() - start;
    if (count == 1) singleTime = elapsed; else assert(elapsed < singleTime * 2);
    assert(s.wire.energizedPeak == count && s.wire.hallReads == 0);
    for (uint8_t u = 0; u < 10; ++u) {
      assert(!s.wire.previous[u]); assert(s.engine.state().units[u].index == (u < count ? 26 : 0));
      assert(s.wire.steps[u] == (u < count ? mega::Bench::positionFor(4096, 26) : 0));
    }
    const auto before = s.wire.steps[0]; s.done(s.text(sf::Op::Text, targets, count)); assert(s.wire.steps[0] == before);
    settings.units[0].speed = 1200; s.done(s.config(settings)); assert(s.engine.state().units[0].armed && s.engine.state().units[0].reference);
    settings.units[0].revolution = 4076; s.done(s.config(settings)); assert(!s.engine.state().units[0].armed && !s.engine.state().units[0].reference);
    for (uint8_t u = 1; u < count; ++u) assert(s.engine.state().units[u].armed && s.engine.state().units[u].reference);
  }
  puts("PASS local 1..10 widths: concurrent physical MCP phases, exact counts, inactive LOW, no Hall dependency, minimum physical intervals, retained calibration");
}
void faultsAndPersistence() {
  System s; sf::Settings sparse; sparse.count = 3; sparse.mapping[0] = 7; sparse.mapping[1] = 0; sparse.mapping[2] = 9;
  s.done(s.config(sparse)); uint8_t a[3] = {1,2,3}, target[3] = {44,44,44};
  s.done(s.text(sf::Op::SetText,a,3)); s.done(s.send(sf::Op::Arm));
  uint32_t move = s.text(sf::Op::Text,target,3); s.run(50000); s.done(s.send(sf::Op::Stop)); assert(s.job(move).error == sf::Error::Stopped);
  for (const auto& u : s.engine.state().units) assert(!u.armed && !u.reference);
  s.done(s.text(sf::Op::SetText,a,3)); s.done(s.send(sf::Op::Arm)); move = s.text(sf::Op::Text,target,3);
  s.wire.nack = true; s.run(30000); assert(s.wire.held && s.job(move).stage == 4 && !s.engine.state().connected);
  assert(s.engine.state().outputsDisabled); s.done(s.send(sf::Op::Stop)); assert(s.engine.maintenance(true));
  assert(s.engine.state().maintenance); s.engine.maintenance(false); s.wire.nack = false; s.run(10000);
  s.done(s.send(sf::Op::Init)); assert(s.engine.state().connected);
  const uint32_t homeJob = s.send(sf::Op::Home); assert(s.job(homeJob).stage == 3);
  const uint32_t hallJob = s.send(sf::Op::Watch,0); assert(s.job(hallJob).stage == 3);
  s.done(s.config(sparse,true)); const uint32_t watch = s.send(sf::Op::Watch,0); s.done(watch); s.run(30000); assert(s.wire.hallReads > 0);
  s.done(s.send(sf::Op::Stop)); assert(s.engine.flash(true)); uint8_t command[] = {uint8_t(sf::Op::Arm),sf::kAll}; uint32_t id;
  assert(s.engine.submit(command,2,0,id) == sf::Error::Busy); s.engine.flash(false);
  const uint32_t request=s.engine.state().nextRequest; assert(s.engine.submit(command,2,request,id)==sf::Error::None); s.run(2000);
  uint32_t repeated=0; assert(s.engine.submit(command,2,request,repeated)==sf::Error::None && repeated==id);
  command[0]=uint8_t(sf::Op::Stop); assert(s.engine.submit(command,2,request,repeated)==sf::Error::Invalid);
  FakeWire rebootWire; Bank bank(rebootWire); Store store(s.nvs); cam::LocalEngine<Bank,Store> reboot(bank,store); reboot.begin(456);
  assert(reboot.state().settings.count==3 && reboot.state().settings.mapping[0]==7 && reboot.state().hallEnabled);
  for (const auto& u:reboot.state().units) assert(!u.armed && !u.reference);
  puts("PASS local sparse map, STOP priority, NACK reset, disconnected OTA lock, Hall gating, NVS reboot and request dedup");
}
void missingAndCorruption() {
  {
    System absent; absent.wire.present=14; absent.send(sf::Op::Init);
    assert(!absent.engine.state().connected && absent.wire.held);
    assert(absent.engine.state().presentMask==14 && absent.engine.state().requiredMask==1);
  }
  {
    System mapped; mapped.done(mapped.send(sf::Op::SetIndex,0,{26})); mapped.done(mapped.send(sf::Op::Arm));
    sf::Settings expanded; expanded.count=2;expanded.mapping[0]=0;expanded.mapping[1]=4;
    mapped.done(mapped.config(expanded));assert(mapped.engine.state().units[0].index==26 && mapped.engine.state().units[0].reference==1);
    assert(!mapped.engine.state().units[0].armed && !mapped.engine.state().units[4].reference);
  }
  System s; s.wire.present = 4; sf::Settings cfg; cfg.mapping[0]=9; s.done(s.config(cfg)); assert(s.engine.state().connected);
  assert(s.engine.state().presentMask==4 && s.engine.state().requiredMask==4);
  s.done(s.config(cfg,true)); assert(!s.engine.state().connected && s.wire.held);
  s.wire.present=12; s.done(s.send(sf::Op::Init)); assert(s.engine.state().connected);
  s.wire.registers[2][0]=0xffff; s.run(400000); assert(s.wire.held && !s.engine.state().connected);
  for (size_t cut=0;cut<=132;++cut) {
    FakeNvs nvs; Store first(nvs); sf::Settings before,after; after.units[7].speed=1500;
    assert(first.save(before,false)); nvs.cut=cut; const bool saved=first.save(after,true);
    Store reboot(nvs); sf::Settings restored; bool hall=false; assert(reboot.load(restored,hall));
    assert(restored.units[7].speed==(saved?1500:800) && hall==saved);
  }
  System wrap; wrap.wire.us=0xfffff000ULL; wrap.run(10000); assert(wrap.engine.state().connected);
  assert(wrap.engine.state().updatedAt==wrap.wire.nowMs());
  puts("PASS missing optional MCP, Hall address requirement, register reset detection, all 133 NVS interruption points and micros rollover");
}
void motionCommands() {
  System s; s.done(s.send(sf::Op::SetIndex,0,{44}));s.done(s.send(sf::Op::Arm));
  const uint32_t start=s.wire.steps[0];s.done(s.send(sf::Op::Cycle),90000000);
  assert(s.wire.steps[0]-start==4096 && s.engine.state().units[0].index==44);
  s.done(s.send(sf::Op::Flap,0));assert(s.engine.state().units[0].index==0);
  s.done(s.send(sf::Op::Goto,0,{2}));assert(s.engine.state().units[0].index==2);
  s.done(s.send(sf::Op::Step,0,{20,0}));assert(!s.engine.state().units[0].reference);
  s.done(s.send(sf::Op::Rev,0));s.done(s.send(sf::Op::Save));
  sf::Settings saved=s.engine.state().settings,changed=saved;changed.units[0].speed=1200;
  s.nvs.ok=false;const uint32_t bad=s.config(changed);assert(s.job(bad).error==sf::Error::Storage);
  assert(s.engine.state().settings.units[0].speed==saved.units[0].speed);
  s.nvs.ok=true;s.done(s.send(sf::Op::Load));s.done(s.config(saved,true));
  s.wire.automaticHall=true;s.wire.physical[0]=25;s.done(s.send(sf::Op::Arm));
  s.done(s.send(sf::Op::Home),60000000);assert(s.engine.state().units[0].reference==2);
  s.done(s.send(sf::Op::Measure,0),120000000);assert(s.engine.state().units[0].measuredCount==3);
  for(uint8_t i=0;i<3;++i)assert(s.engine.state().units[0].measured[i]==4096);
  s.done(s.send(sf::Op::SetIndex,0,{0}));s.done(s.send(sf::Op::Arm));s.send(sf::Op::Step,0,{100,0});
  s.wire.us+=100001;s.engine.tick();assert(s.wire.held && !s.engine.state().units[0].armed);
  puts("PASS local CYCLE/FLAP/GOTO/RAW/REV, rejected NVS preserves RAM, Hall HOME/three measured revolutions, real-clock stall");
}
int main() { widths(); faultsAndPersistence(); missingAndCorruption(); motionCommands(); }
