#pragma once
#include "screen.h"

// SSD1306 renderer: draws a Screen and nothing else. ESP32 only (not in the
// host tests); all decisions about what to show live in screen.cpp.
bool displayInit();                    // false if no OLED answers at OLED_ADDR
void displaySplash(const char* line);  // boot screen while sound calibrates
void displayDraw(const Screen& s);     // no-op when displayInit() failed
