// Host-side stand-in for the Arduino core: a settable clock and fake pins.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iterator>

using std::isnan;  // the ESP32 core exposes it globally via math.h

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

namespace fake {
inline uint32_t now = 0;
inline int pins[40] = {};
inline int modes[40] = {};
inline uint16_t analog[40] = {};
inline void reset() {
  now = 0;
  std::fill(std::begin(pins), std::end(pins), 0);
  std::fill(std::begin(modes), std::end(modes), -1);
  std::fill(std::begin(analog), std::end(analog), 0);
}
}  // namespace fake

inline uint32_t millis() { return fake::now; }
inline void delay(uint32_t ms) { fake::now += ms; }
inline int digitalRead(uint8_t p) { return fake::pins[p]; }
inline void digitalWrite(uint8_t p, int v) { fake::pins[p] = v; }
inline void pinMode(uint8_t p, int m) { fake::modes[p] = m; }
inline uint16_t analogRead(uint8_t p) { return fake::analog[p]; }

template <class T, class L, class H>
inline T constrain(T v, L lo, H hi) { return v < lo ? lo : (v > hi ? hi : v); }
