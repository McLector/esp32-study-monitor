// Host-side stand-in for Adafruit's DHT library.
#pragma once
#include <Arduino.h>

#define DHT11 11

namespace fake {
inline float dhtTemp = NAN, dhtHum = NAN;
}

class DHT {
 public:
  DHT(uint8_t, uint8_t) {}
  void begin() {}
  float readTemperature() { return fake::dhtTemp; }
  float readHumidity() { return fake::dhtHum; }
};
