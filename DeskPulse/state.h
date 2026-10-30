#pragma once
#include "config.h"

enum class Mode : uint8_t { Away, Studying, BreakDue, OnBreak };

struct Settings {  // saved in NVS (phase 8)
  uint8_t sitLimitMin = DEF_SIT_LIMIT_MIN;
  uint8_t breakMin = DEF_BREAK_MIN;
  bool eyeRestOn = true;
  bool muted = false;
  uint16_t noiseMargin = DEF_NOISE_MARGIN;
};

struct Daily {  // saved in NVS, reset at local midnight (phase 9)
  uint8_t version = TEST_MODE ? 101 : 1;  // test-mode data never loads in a real build
  uint32_t ymd = 0;                       // e.g. 20261022
  uint32_t studySec = 0, idleSec = 0;
  uint16_t distractions = 0;
  uint16_t breaksTaken = 0, breaksOnTime = 0, breaksSkipped = 0;
};

struct Session {
  bool active = false;
  uint32_t studySec = 0;
  uint16_t breaks = 0, distractions = 0;
};

struct Live {
  Mode mode = Mode::Away;
  bool present = false, objectSuspected = false;
  uint32_t sitSec = 0, awaySec = 0, dueSec = 0, eyeSec = 0;
  uint8_t eyeRestLeft = 0;   // > 0 while the eye-rest overlay is up
  bool skipCounted = false;  // one skip per break cycle
  float tempC = NAN, humidity = NAN;
  uint16_t noisePeak = 0;
  int8_t focus = -1;  // -1 = not enough data yet
};

struct AppState { Settings cfg; Daily day; Session ses; Live live; };
extern AppState app;  // defined once in DeskPulse.ino
