// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include <deque>
#include <string>
#define F(text) text
static const uint8_t LOW = 0, HIGH = 1, INPUT_PULLUP = 2, OUTPUT = 3;
extern uint32_t fakeNow;
extern uint8_t fakePins[70], fakeModes[70];
inline uint32_t micros() { return fakeNow; }
inline uint32_t millis() { return fakeNow / 1000; }
inline int digitalRead(uint8_t pin) { return fakePins[pin]; }
inline void digitalWrite(uint8_t pin, uint8_t value) { fakePins[pin] = value; }
inline void pinMode(uint8_t pin, uint8_t mode) {
  fakeModes[pin] = mode;
  if (mode == INPUT_PULLUP) fakePins[pin] = HIGH;
}
struct FakeSerial {
  std::deque<char> input;
  std::string output;
  void begin(unsigned long) {}
  int available() const { return static_cast<int>(input.size()); }
  int availableForWrite() const { return 64; }
  size_t write(const uint8_t* bytes, size_t count) { output.append(reinterpret_cast<const char*>(bytes), count); return count; }
  int read() { const char c = input.front(); input.pop_front(); return c; }
  void print(const char* s) { output += s; }
  void print(char c) { output += c; }
  template <typename T> void print(T value) { output += std::to_string(value); }
  template <typename T> void println(T value) { print(value); output += '\n'; }
  void feed(const std::string& text) { for (char c : text) input.push_back(c); }
};
extern FakeSerial Serial;
extern FakeSerial Serial1;
