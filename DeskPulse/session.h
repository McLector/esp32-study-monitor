#pragma once
#include "state.h"

enum class Signal : uint8_t {
  ModeChanged, BreakDue, EyeRest, Distraction, SessionEnded, DayRolled
};
void onSignal(Signal s);  // implemented in DeskPulse.ino

void sessionInit();             // resets the catch-up clock
void sessionTick();             // 1 Hz state machine
void taskTick();                // every 250 ms: runs sessionTick() with catch-up
void tickEyeRest();             // Enhancement 3 (phase 10); no-op for now
int8_t computeFocus(const Daily& d);  // Enhancement 2b (phase 9); -1 for now
void resetToday();              // zero Daily + Session, keep settings
uint32_t sitLimitSec();
uint32_t breakSec();
