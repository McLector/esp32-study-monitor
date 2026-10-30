#pragma once
#include "state.h"

// What the OLED should show, worked out from the app state. Pure logic, no
// display library, so it runs in the host tests; display.cpp only draws it.
struct Screen {
  bool eyeRest = false;  // full-screen eye-rest overlay instead of the status page
  uint8_t eyeLeft = 0;   // overlay countdown, seconds
  char title[11] = "";   // status word, text size 2 (10 chars max)
  char timers[22] = "";  // "Sit mm:ss  Left mm:ss" or "Break counts in mm:ss"
  uint8_t barPx = 0;     // progress bar fill inside the frame, 0..126
  char env[14] = "";     // "28.5C 65%RH" or "--.-C --%RH"
  char dist[8] = "";     // "D:3", right-aligned
  char focus[10] = "";   // "Focus 87" or "Focus --"
  const char* link = ""; // "Online", "No cloud" or "Offline", right-aligned
  bool dim = false;      // burn-in: dim while away
  bool off = false;      // burn-in: panel off once the session has ended
};

constexpr uint8_t SCREEN_BAR_MAX_PX = 126;

// mm:ss, capped at 99:59 so a row never outgrows the 21-character width.
void formatMmss(char* out, size_t n, uint32_t sec);

// Reads the global app state, like ledsUpdate() does.
Screen buildScreen(bool wifiUp, bool cloudUp);
