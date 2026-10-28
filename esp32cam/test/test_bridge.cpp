// SPDX-License-Identifier: GPL-3.0-only
#include "Arduino.h"
#include "remote.h"
#include "../../esp32cam/controller/bridge_engine.h"
#include "../../esp32cam/controller/display_memory.h"
#include "../../esp32cam/controller/json_helpers.h"
#include <assert.h>
#include <stdio.h>
#include <deque>
#include <string>
uint32_t fakeNow = 0;
uint8_t fakePins[70] = {}, fakeModes[70] = {};
FakeSerial Serial, Serial1;
struct Memory {
  uint8_t bytes[4096]; Memory() { memset(bytes, 0xff, sizeof(bytes)); }
  uint8_t read(uint16_t a) const { return bytes[a]; }
  void update(uint16_t a, uint8_t v) { bytes[a] = v; }
};
// UART line simulator: at most 11 bytes/ms, Mega RX capacity 64. Loss occurs on complete frames.
struct Wire {
  std::string frame; std::deque<char> ready; sf::Decoder decoder;
  sf::Kind dropKind = sf::Kind::Boot; int dropCount = 0;
  void pipe(FakeSerial& from, FakeSerial& to, uint32_t ms, bool cut, size_t capacity) {
    if (cut) { from.output.clear(); frame.clear(); ready.clear(); decoder = sf::Decoder(); return; }
    for (char ch : from.output) {
      frame += ch; sf::Packet p;
      if (decoder.feed(uint8_t(ch), ms, p)) {
        if (p.kind == dropKind && dropCount) --dropCount;
        else for (char b : frame) ready.push_back(b);
        frame.clear();
      } else if (!ch) frame.clear();
    }
    from.output.clear();
    for (int i = 0; i < 11 && !ready.empty(); ++i) { to.input.push_back(ready.front()); ready.pop_front(); }
    assert(to.input.size() <= capacity);
  }
};
struct System {
  Memory memory; mega::Panel panel; mega::Remote<Memory> mega;
  cam::BridgeEngine esp; FakeSerial espUart, megaUart; Wire up, down;
  uint32_t ms = 0; bool cut = false, cutUp = false;
  System() : mega(panel, memory) { mega.begin(); esp.begin(0x98341912); run(450); assert(esp.state().connected && esp.state().valid); }
  void run(uint32_t count) {
    while (count--) {
      ++ms; esp.tick(espUart, ms); up.pipe(espUart, megaUart, ms, cut || cutUp, 64);
      panel.tick(ms * 1000, 0x3ff); mega.poll(megaUart, ms, ms * 1000, 0x3ff);
      down.pipe(megaUart, espUart, ms, cut, 1024);
      uint8_t energized = 0;
      for (uint8_t u = 0; u < 10; ++u) if (panel.unit(u).coils()) { assert(panel.enabled(u)); ++energized; }
      assert(energized <= panel.digits());
    }
  }
  uint32_t submit(sf::Op op, uint8_t unit, std::initializer_list<uint8_t> args = {}) {
    uint8_t data[sf::kMaxPayload] = {uint8_t(op), unit}; size_t n = 2;
    for (uint8_t value : args) data[n++] = value;
    uint32_t id = 0; assert(esp.submit(data, n, esp.state().nextRequest, ms, id) == sf::Error::None); return id;
  }
  void done(uint32_t id, uint32_t limit = 120000) {
    for (uint32_t i = 0; i < limit; ++i) {
      run(1); const cam::Job* j = esp.job(id); assert(j);
      if (j->stage >= 2) { assert(j->stage == uint8_t(sf::Stage::Done)); run(200); return; }
    }
    assert(!"command timeout");
  }
};
void display() {
  const char* values[] = {" ","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z","Ä","Ö","Ü","0","1","2","3","4","5","6","7","8","9",":",".","-","?","!"};
  uint8_t out[10];
  for (uint8_t i = 0; i < 45; ++i) { assert(cam::textIndices(values[i], 1, cam::Align::Left, true, out)); assert(out[0] == i); }
  assert(cam::textIndices("äöü", 3, cam::Align::Left, true, out) && out[0] == 27 && out[2] == 29);
  assert(cam::textIndices("A\xcc\x88O\xcc\x88U\xcc\x88", 3, cam::Align::Left, true, out) && out[1] == 28);
  assert(cam::textIndices("A", 5, cam::Align::Center, false, out) && out[2] == 1 && out[1] == 0);
  assert(cam::textIndices("_z", 5, cam::Align::Right, false, out) && out[4] == 26);
  assert(!cam::textIndices("AB", 1, cam::Align::Left, false, out));
  assert(!cam::textIndices("中文", 10, cam::Align::Left, false, out));
  assert(!cam::textIndices("A\n", 10, cam::Align::Left, false, out));
  assert(!cam::textIndices("A", 3, cam::Align::Left, true, out));
  uint8_t expected[10];
  assert(cam::formatTime(1704067200, 480, "clock", "HH:MM", 5, cam::Align::Left, out));
  assert(cam::textIndices("08:00", 5, cam::Align::Left, true, expected) && !memcmp(out, expected, 5));
  assert(cam::formatTime(1704067200, -720, "date", "YYYY-MM-DD", 10, cam::Align::Left, out));
  assert(cam::textIndices("2023-12-31", 10, cam::Align::Left, true, expected) && !memcmp(out, expected, 10));
  assert(cam::formatTime(1709164800, 480, "date", "MM-DD", 5, cam::Align::Left, out));
  assert(cam::textIndices("02-29", 5, cam::Align::Left, true, expected) && !memcmp(out, expected, 5));
  for (uint8_t width = 1; width <= 10; ++width) {
    assert(cam::validFormat("clock", "HHMM", width) == (width >= 4));
    assert(cam::validFormat("date", "YYYYMMDD", width) == (width >= 8));
  }
  assert(!cam::formatTime(0, 480, "clock", "HHMM", 4, cam::Align::Left, out));
  assert(!cam::formatTime(1704067200, 841, "clock", "HHMM", 4, cam::Align::Left, out));
  puts("PASS all 45 UTF-8 glyphs, aliases/NFC, padding/alignment, leap day, timezone and every width");
}
void jsonApi() {
  const std::string hidden = std::string("{}") + '\0' + "ARM";
  assert(!cam::parseBody(hidden.c_str(), hidden.size()));
  for (const char* bad : {"{} ARM", "{\"op\":\"TEXT\",\"value\":\"\\u0000\"}", "[]", "null"}) assert(!cam::parseBody(bad, strlen(bad)));
  const std::string deep(17, '['); assert(!cam::parseBody(deep.c_str(), deep.size()));
  cJSON* body = cam::parseBody("{\"op\":\"STOP\"} \n", 15); assert(body); cJSON_Delete(body);
  sf::Settings settings; settings.count = 3; settings.mapping[0] = 7; settings.mapping[1] = 2; settings.mapping[2] = 0;
  cJSON* json = cam::settingsJson(settings); sf::Settings parsed;
  assert(cam::parseSettings(json, parsed) && parsed.mapping[0] == 7);
  cJSON* cal = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json, "calibration"), 9);
  cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cal, "speed"), 799);
  assert(!cam::parseSettings(json, parsed)); cJSON_Delete(json);
  for (const char* data : {"{\"units\":[0,0],\"calibration\":[]}", "{}", "null", "[]"}) {
    json = cJSON_Parse(data); assert(!cam::parseSettings(json, parsed)); cJSON_Delete(json);
  }
  uint8_t payload[160], n = 0;
  json = cJSON_Parse("{\"op\":\"TEXT\",\"value\":\"äa\",\"unit\":\"ALL\"}");
  assert(cam::commandPayload(json, 3, cam::Align::Right, payload, n) && n == 6 && payload[3] == 0 && payload[4] == 27 && payload[5] == 1); cJSON_Delete(json);
  for (const char* data : {"{\"op\":\"TEXT\",\"value\":\"ABCD\"}", "{\"op\":\"SETTEXT\",\"value\":\"A\"}",
      "{\"op\":\"STEP\",\"value\":-1}", "{\"op\":\"STEP\",\"value\":1.5}", "{\"op\":\"SHOW\",\"value\":\"你好\"}",
      "{\"op\":\"ARM\",\"unit\":10}", "{\"op\":\"DIR\",\"value\":0}", "{\"op\":\"INJECT\\nARM\"}"}) {
    json = cJSON_Parse(data); assert(!cam::commandPayload(json, 3, cam::Align::Left, payload, n)); cJSON_Delete(json);
  }
  json = cJSON_Parse("{\"op\":\"UNITS\",\"value\":[7,0,2]}");
  assert(cam::commandPayload(json, 3, cam::Align::Left, payload, n) && n == 6 && payload[3] == 7); cJSON_Delete(json);
  uint8_t header[64] = {}; memcpy(header, "SFESP01", 8); sf::put32(header + 8, 123456); sf::put32(header + 12, 1); memcpy(header + 48, "splitflap-32s-v1", 16); uint32_t size;
  assert(cam::otaHeader(header, 1966080, size) && size == 123456);
  memcpy(header + 48, "splitflap-cam-v1", 16);
  assert(!cam::otaHeader(header, 1966080, size));
  memcpy(header + 48, "splitflap-32s-v1", 16);
  memcpy(header, "SFCAM01", 8);
  assert(!cam::otaHeader(header, 1966080, size));
  memcpy(header, "SFCAM01", 8); memcpy(header + 48, "splitflap-cam-v1", 16);
  assert(!cam::otaHeader(header, 1966080, size));
  memcpy(header, "SFESP01", 8); memcpy(header + 48, "splitflap-32s-v1", 16);
  sf::put32(header + 12, 2); assert(!cam::otaHeader(header, 1966080, size));
  sf::put32(header + 12, 1);
  header[0] ^= 1; assert(!cam::otaHeader(header, 1966080, size)); header[0] ^= 1;
  sf::put32(header + 8, 255); assert(!cam::otaHeader(header, 1966080, size));
  sf::put32(header + 8, 1966081); assert(!cam::otaHeader(header, 1966080, size));
  puts("PASS production JSON parser: UTF-8, exact/manual lengths, bad ranges/types, whole-config validation and OTA header");
}
void endToEnd() {
  {
    cam::BridgeEngine mismatch; mismatch.begin(7); FakeSerial port;
    sf::Packet p; p.kind = sf::Kind::Welcome; p.session = mismatch.state().session; p.length = 2; p.data[0] = 2;
    uint8_t wire[sf::kWireSize]; size_t n = sf::encode(p, wire); port.feed(std::string(reinterpret_cast<char*>(wire), n));
    mismatch.tick(port, 1); assert(mismatch.state().protocolMismatch && !mismatch.state().connected);
  }
  System h;
  for (uint8_t width = 1; width <= 10; ++width) {
    h.done(h.submit(sf::Op::Digits, sf::kAll, {width}));
    uint8_t set[13] = {uint8_t(sf::Op::SetText), sf::kAll, width};
    uint32_t id;
    assert(h.esp.submit(set, 3 + width, h.esp.state().nextRequest, h.ms, id) == sf::Error::None); h.done(id);
    h.done(h.submit(sf::Op::Arm, sf::kAll)); set[0] = uint8_t(sf::Op::Text);
    for (uint8_t i = 0; i < width; ++i) set[3 + i] = 1;
    const uint32_t request = h.esp.state().nextRequest;
    assert(h.esp.submit(set, 3 + width, request, h.ms, id) == sf::Error::None);
    uint32_t duplicate;
    assert(h.esp.submit(set, 3 + width, request, h.ms, duplicate) == sf::Error::None && duplicate == id);
    h.run(20); assert(h.esp.pending()); h.done(id);
    for (uint8_t i = 0; i < width; ++i) assert(h.panel.unit(i).index() == 1);
  }
  const uint8_t data[] = {uint8_t(sf::Op::Step), 0, 100, 0}; uint32_t rejectedId = 0;
  assert(h.esp.submit(data, sizeof(data), 1, h.ms, rejectedId) == sf::Error::Unknown); // evicted external ID cannot be replayed
  h.down.dropKind = sf::Kind::Result; h.down.dropCount = 2;
  h.done(h.submit(sf::Op::SetIndex, 0, {2})); assert(h.panel.unit(0).index() == 2); // recover lost accepted/done by result query
  h.done(h.submit(sf::Op::Arm, 0)); const uint32_t longJob = h.submit(sf::Op::Step, 0, {0x20, 0x4e}); h.run(100);
  h.cut = true; h.run(2300);
  assert(!h.panel.busy() && !h.panel.unit(0).armed() && !h.esp.state().connected);
  assert(h.esp.job(longJob)->stage == uint8_t(sf::Stage::Fault));
  h.cut = false; h.run(1600); assert(h.esp.state().connected && h.esp.state().uncertain);
  h.done(h.submit(sf::Op::Claim, sf::kAll)); assert(!h.esp.state().uncertain);
  h.up.dropKind = sf::Kind::Command; h.up.dropCount = 1;
  const uint32_t lost = h.submit(sf::Op::SetIndex, 0, {5}); h.run(1400);
  assert(h.esp.job(lost)->stage == uint8_t(sf::Stage::Rejected) && h.esp.job(lost)->error == sf::Error::Unknown);
  assert(h.esp.state().uncertain && !h.panel.unit(0).positioned());
  h.done(h.submit(sf::Op::Stop, sf::kAll));
  h.done(h.submit(sf::Op::SetIndex, 0, {0})); h.done(h.submit(sf::Op::Arm, 0));
  const uint32_t interrupted = h.submit(sf::Op::Step, 0, {0x20, 0x4e}); h.run(100);
  h.mega.usbActivity(); h.panel.stopAll(); h.run(1200);
  assert(h.esp.job(interrupted)->stage == uint8_t(sf::Stage::Fault) && h.esp.state().panel.usbLocked);
  h.done(h.submit(sf::Op::Claim, sf::kAll));
  assert(h.esp.maintenance(true)); const uint8_t arm[] = {uint8_t(sf::Op::Arm), 0}; uint32_t job = 0;
  assert(h.esp.submit(arm, 2, h.esp.state().nextRequest, h.ms, job) == sf::Error::Busy); assert(h.esp.maintenance(false));
  System asymmetric;
  asymmetric.done(asymmetric.submit(sf::Op::SetIndex, 0, {0}));
  asymmetric.done(asymmetric.submit(sf::Op::Arm, 0));
  asymmetric.submit(sf::Op::Step, 0, {0x20, 0x4e}); asymmetric.run(100);
  asymmetric.cutUp = true; asymmetric.run(2300);
  assert(!asymmetric.panel.busy() && !asymmetric.panel.unit(0).armed());
  assert(asymmetric.esp.state().connected && asymmetric.esp.state().uncertain);
  asymmetric.cutUp = false; asymmetric.run(700);
  asymmetric.done(asymmetric.submit(sf::Op::Claim, sf::kAll));
  // Deliberately slow speed: all ten move at once and finish within one rotor's time.
  h.done(h.submit(sf::Op::Speed, sf::kAll, {0x88, 0x13}));
  uint8_t slowText[13] = {uint8_t(sf::Op::SetText), sf::kAll, 10}; uint32_t slowJob = 0;
  assert(h.esp.submit(slowText, sizeof(slowText), h.esp.state().nextRequest, h.ms, slowJob) == sf::Error::None);
  h.done(slowJob); h.done(h.submit(sf::Op::Arm, sf::kAll));
  slowText[0] = uint8_t(sf::Op::Text); memset(slowText + 3, 44, 10);
  assert(h.esp.submit(slowText, sizeof(slowText), h.esp.state().nextRequest, h.ms, slowJob) == sf::Error::None);
  h.run(1000); for (uint8_t u = 0; u < 10; ++u) assert(h.panel.unit(u).steps() > 0);
  const uint32_t started = h.ms; h.done(slowJob, 30000); assert(h.ms - started < 30000);
  for (uint8_t u = 0; u < 10; ++u) assert(h.panel.unit(u).index() == 44);
  uint8_t importPayload[sf::kSettingsSize + 2] = {uint8_t(sf::Op::ApplyConfig), sf::kAll};
  sf::encodeSettings(h.esp.state().settings, importPayload + 2); uint32_t importJob = 0;
  assert(h.esp.submit(importPayload, sizeof(importPayload), h.esp.state().nextRequest, h.ms, importJob) == sf::Error::None);
  h.done(importJob); h.esp.configurationFailed(importJob); h.run(1200);
  assert(h.esp.job(importJob)->stage == uint8_t(sf::Stage::Fault) && h.esp.job(importJob)->error == sf::Error::Storage);
  puts("PASS ESP↔Mega at 115200/64-byte RX: all widths, parallel outputs, external dedup, lost ACK/DONE/command, cut/reconnect, USB and OTA lock");
  puts("PASS ten-unit slow batch starts concurrently and completes within one rotor's duration");
}
void networkProvisioning() {
  cam::State s;
  assert(cam::canConfigureNetwork(s, false, false)); // Fresh ESP, no Mega attached.
  s.panel.busy = s.panel.saving = s.panel.watching = true;
  assert(cam::canConfigureNetwork(s, false, false)); // Stale state after link loss cannot lock provisioning.
  s.connected = s.valid = true;
  assert(!cam::canConfigureNetwork(s, false, false));
  s.panel.busy = s.panel.saving = s.panel.watching = false;
  assert(cam::canConfigureNetwork(s, false, false));
  s.panel.watching = true; assert(!cam::canConfigureNetwork(s, false, false)); s.panel.watching = false;
  s.panel.saving = true; assert(!cam::canConfigureNetwork(s, false, false)); s.panel.saving = false;
  s.panel.busy = true; assert(!cam::canConfigureNetwork(s, false, false)); s.panel.busy = false;
  s.connected = s.valid = false;
  assert(!cam::canConfigureNetwork(s, true, false));
  assert(!cam::canConfigureNetwork(s, false, true));
  s.maintenance = true; assert(!cam::canConfigureNetwork(s, false, false));
  puts("PASS Wi-Fi provisioning without Mega, stale link state, and busy/save/watch/OTA guards");
}
void persistentGlyphs() {
  cam::DisplayMemory memory; sf::UnitState units[10];
  assert(memory.glyph(0) == cam::DisplayMemory::kUnknown && !memory.observe(units));
  for (uint8_t glyph = 0; glyph < 45; ++glyph) {
    units[7].reference = 1; units[7].index = glyph;
    assert(memory.observe(units)); uint8_t record[cam::DisplayMemory::kSize]; memory.encode(record);
    cam::DisplayMemory reboot; assert(reboot.decode(record, sizeof(record)) && reboot.glyph(7) == glyph);
    assert(reboot.glyph(0) == cam::DisplayMemory::kUnknown && !reboot.dirty());
    memory.saved(); assert(!memory.observe(units));
    record[3] ^= 1; assert(!reboot.decode(record, sizeof(record)) && reboot.glyph(7) == glyph);
  }
  units[7].mode = 7; units[7].index = 1; assert(!memory.observe(units));
  units[7].mode = 0; units[7].reference = 0; assert(!memory.observe(units) && memory.glyph(7) == 44);
  uint8_t record[cam::DisplayMemory::kSize]; memory.encode(record); record[1] = 45;
  sf::put16(record + 11, sf::crc16(record, 11)); assert(!memory.decode(record, sizeof(record)));
  puts("PASS durable glyph history: all 45 characters, physical mapping, CRC/range rejection, no mid-motion or unknown-position overwrite");
}
int main() { display(); jsonApi(); endToEnd(); networkProvisioning(); persistentGlyphs(); }
