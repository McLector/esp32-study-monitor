#pragma once
#include "state.h"

enum class Pattern : uint8_t { BreakAlert, Chirp3 };

void buzzerInit();               // pins off, sequencer idle
void buzzerPlay(Pattern p);      // queues a pattern (replaces any running one)
void buzzerUpdate();             // every loop() pass: advances the sequencer
bool buzzerIsOn();
bool buzzerActiveWithin(uint32_t ms);  // on now, or switched off less than ms ago

// Repeats the break alert every ALERT_REPEAT_MS while in Break due, unless muted.
void alertUpdate();

// Bit i = LED i lit (bit 0 blue … bit 4 red).
uint8_t ledMask(Mode m, uint32_t sitSec, uint32_t limitSec, uint32_t nowMs);
constexpr uint16_t LED_SWEEP_STEP_MS = 80;  // eye-rest sweep: blue to red, one LED per step
uint8_t sweepMask(uint32_t elapsedMs);    // the sweep's LED at this point, 0 once done
void ledSweep();                          // start one sweep; it overrides the normal LEDs
void ledsUpdate();               // writes ledMask() for the current state to the pins
