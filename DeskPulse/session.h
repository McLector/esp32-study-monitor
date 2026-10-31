#pragma once
#include "state.h"

enum class Signal : uint8_t {
  ModeChanged, BreakDue, EyeRest, Distraction, SessionEnded, DayRolled
};
void onSignal(Signal s);  // implemented in DeskPulse.ino

void sessionInit();             // resets the catch-up clock
void sessionTick();             // 1 Hz state machine
void taskTick();                // every 250 ms: runs sessionTick() with catch-up
void tickEyeRest();             // Enhancement 3: 20-20-20 reminder, while present
int8_t computeFocus(const Daily& d);  // Enhancement 2c: 0-100, or -1 under 10 study minutes
// Enhancement 2d: feed today's local date (e.g. 20261031) or 0 if unknown. On a new
// day raises DayRolled (final score still readable) and then zeroes today's counters.
bool dayRollover(uint32_t ymd);
void resetToday();              // zero Daily + Session, keep settings
uint32_t sitLimitSec();
uint32_t breakSec();
