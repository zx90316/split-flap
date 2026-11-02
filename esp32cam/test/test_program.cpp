// SPDX-License-Identifier: GPL-3.0-only
#include "../controller/program_json.h"
#include "../controller/session_table.h"
#include <cassert>
#include <cstdio>
void expectTime(int64_t time, const char* mode, const char* format, const char* expected, bool zeros = true) {
  uint8_t result[10], want[10];
  assert(cam::formatTime(time, 0, mode, format, 10, cam::Align::Left, result, zeros));
  assert(cam::textIndices(expected, 10, cam::Align::Left, false, want));
  assert(!memcmp(result, want, 10));
}
void formats() {
  expectTime(1704067200, "clock", "hh:mm:ssAP", "12:00:00AM");
  expectTime(1704110400, "clock", "hh:mmAP", "12:00PM");
  expectTime(1704070923, "clock", "HH:MM:SS", "01:02:03");
  expectTime(1704070923, "clock", "HHMM", " 102", false);
  expectTime(1704067200, "weekday", "SHORT", "MON");
  expectTime(1704067200, "weekday", "LONG", "MONDAY");
  expectTime(1709164800, "date", "DD-MM-YYYY", "29-02-2024");
  expectTime(1735689599, "date", "YYMMDD", "241231");
  expectTime(1735689600, "date", "DDMM", "0101");
  assert(!cam::validFormat("clock", "HH:MM:SS", 7));
  assert(cam::validFormat("clock", "SS", 2));
  assert(!cam::validFormat("weekday", "LONG", 8));
  assert(cam::inWindow(23*60, true, 22*60, 8*60));
  assert(cam::inWindow(7*60, true, 22*60, 8*60));
  assert(!cam::inWindow(8*60, true, 22*60, 8*60));
  assert(cam::inWindow(720, true, 0, 0));
  cam::AppSettings window; window.limited = true; window.timezone = 480; window.windowStart = 22*60; window.windowEnd = 8*60;
  assert(!cam::programWindowOpen(window, 0));
  assert(!cam::programWindowOpen(window, 1704153600)); // Taipei 08:00 is the excluded endpoint.
  assert(cam::programWindowOpen(window, 1704153599));
  puts("PASS time/date/weekday formats, midnight/noon/leap day, width and cross-midnight windows");
}
void settings() {
  Preferences::records().clear(); cam::LegacySettings old;
  strcpy(old.ssid, "ExistingNetwork"); strcpy(old.wifiPassword, "ExistingWifiSecret");
  strcpy(old.apPassword, "ExistingAPSecret"); strcpy(old.adminPassword, "ExistingAdminSecret"); old.timezone = -240;
  uint8_t legacy[sizeof(old) + 2]; memcpy(legacy, &old, sizeof(old)); sf::put16(legacy + sizeof(old), sf::crc16(legacy, sizeof(old)));
  Preferences p; p.putBytes("settings", legacy, sizeof(legacy));
  cam::AppSettings s; cam::AppStore store; assert(store.begin(s));
  assert(!strcmp(s.ssid, old.ssid) && !strcmp(s.adminPassword, old.adminPassword) && !strcmp(s.wifiPassword, old.wifiPassword));
  assert(!s.autoRecovery && !s.recoveryChosen && !s.calibratedMask && !s.resumeDisplay && s.timezone == -240);
  assert(p.getBytesLength("settings") == sizeof(legacy));
  s.autoRecovery = s.recoveryChosen = true; s.calibratedMask = 1;
  sf::Settings mega; s.signatures[0] = cam::geometrySignature(mega.units[0]);
  assert(cam::calibrated(s, mega)); mega.units[0].speed = 900; assert(cam::calibrated(s, mega));
  ++mega.units[0].offset; assert(!cam::calibrated(s, mega));
  assert(store.save(s)); cam::AppSettings restarted; cam::AppStore second; assert(second.begin(restarted));
  assert(restarted.autoRecovery && !strcmp(restarted.adminPassword, old.adminPassword));
  assert(restarted.recoveryChosen); // A later calibration must respect an explicit user choice.
  Preferences::failWrites() = true; auto changed = s; strcpy(changed.ssid, "ShouldNotPersist"); assert(!store.save(changed));
  Preferences::failWrites() = false; cam::AppSettings still; assert(second.begin(still)); assert(!strcmp(still.ssid, old.ssid));
  Preferences::records()["settings-v2"][1] ^= 1; cam::AppSettings corrupt; assert(!second.begin(corrupt));
  assert(Preferences::records()["settings-v2"][1] != 0); // Invalid settings are not overwritten with defaults.
  puts("PASS v1 migration keeps credentials, calibration signatures, durable v2 roundtrip and failed/corrupt storage");
}
void programs() {
  cam::AppSettings s; strcpy(s.apPassword,"MockPassword123"); strcpy(s.adminPassword,"MockPassword123");
  cJSON* p = cJSON_Parse("{\"mode\":\"carousel\",\"items\":[{\"mode\":\"clock\",\"format\":\"HH:MM:SS\",\"text\":\"\",\"seconds\":10,\"align\":1},{\"mode\":\"text\",\"format\":\"\",\"text\":\"HELLO\",\"seconds\":30,\"align\":0}]}");
  assert(cam::parseProgram(p,s)); cJSON_Delete(p); assert(!cam::programFits(s,5)); assert(cam::programFits(s,8));
  p=cam::programJson(s); cam::AppSettings copy=s; assert(cam::parseProgram(p,copy)); cJSON_Delete(p); assert(copy.itemCount==2);
  s.items[0].seconds=9; assert(!cam::validAppSettings(s)); s.items[0].seconds=10;
  s.items[0].format[0]='X'; assert(!cam::validAppSettings(s));
  sf::Settings mega; p=cam::settingsJson(mega); cJSON_DeleteItemFromObjectCaseSensitive(p,"units"); sf::Settings restored;
  assert(cam::parseSettings(p,restored)&&cam::contiguous(restored)); cJSON_Delete(p);
  puts("PASS carousel validation, bounded dwell, schema2 digits and JSON roundtrip");
}
void sessions() {
  cam::SessionTable table; uint8_t a[32]={1}, b[32]={2}, bad[32]={3}, data[cam::SessionTable::kSize];
  assert(!table.contains(a)); table.add(a); table.add(b); assert(table.contains(a)&&table.contains(b)&&!table.contains(bad));
  table.encode(data); cam::SessionTable restarted; assert(restarted.decode(data,sizeof(data))&&restarted.contains(a));
  restarted.remove(a); assert(!restarted.contains(a)&&restarted.contains(b)); restarted.clear(); assert(!restarted.contains(b));
  data[80]^=1; assert(!restarted.decode(data,sizeof(data)));
  puts("PASS independent persistent sessions, logout/revoke and corrupt record rejection");
}
int main() { formats(); settings(); programs(); sessions(); }
