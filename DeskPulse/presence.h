#pragma once
#include "state.h"

void presenceInit();  // pins, debounce counters, object-guard clock
// One debounced read: irActive already has IR_ACTIVE_LOW applied.
// Updates app.live.present and app.live.objectSuspected.
void presenceStep(bool irActive, bool pirMotion, uint32_t nowMs);
void taskPresence();  // every 200 ms: reads the pins, calls presenceStep()
