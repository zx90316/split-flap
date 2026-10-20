// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Logical adapter test double only: not a UART, timing, electrical or hardware model.
#include <stddef.h>
#include <stdint.h>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#define LOW 0
#define HIGH 1
#define OUTPUT_OPEN_DRAIN 2

extern uint32_t fakeNow;
extern int fakeResetLevel;
extern std::vector<int> fakeGpioEvents;
void fakeResetExpanders();
inline uint32_t micros() { return fakeNow; }
inline void delayMicroseconds(unsigned value) { fakeNow += value; }
inline void delay(unsigned value) { fakeNow += value * 1000; }
inline void pinMode(int, int mode) { fakeGpioEvents.push_back(10 + mode); }
inline void digitalWrite(int, int value) {
  fakeGpioEvents.push_back(value);
  fakeResetLevel = value;
  if (value == LOW) fakeResetExpanders();
}
inline void yield() {}

struct FakeSerial {
  std::string output;
  std::deque<unsigned char> input;
  std::vector<int> resetAtWrite;
  void begin(unsigned) {}
  size_t print(const char* text) {
    resetAtWrite.push_back(fakeResetLevel);
    output += text;
    return std::string(text).size();
  }
  size_t println(const char* text) { const size_t n = print(text); output += '\n'; return n + 1; }
  size_t printf(const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    print(buffer);
    return n < 0 ? 0 : static_cast<size_t>(n);
  }
  int available() { return static_cast<int>(input.size()); }
  int read() {
    if (input.empty()) return -1;
    const int value = input.front(); input.pop_front(); return value;
  }
  void feed(const std::string& text) { for (unsigned char c : text) input.push_back(c); }
  void clearOutput() { output.clear(); resetAtWrite.clear(); }
};
extern FakeSerial Serial;
