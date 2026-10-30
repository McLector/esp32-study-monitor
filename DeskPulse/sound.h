#pragma once
#include "state.h"

void soundInit();                      // clears the window, cooldown and baseline
void calibrateSound();                 // blocks ~2 s: learns the ambient peak-to-peak
void soundSetBaseline(uint16_t p2p);
uint16_t soundBaseline();
void soundSample();                    // every loop() pass
void taskSound();                      // every 50 ms
// Counts a distraction if present, above baseline + margin and out of cooldown.
bool checkDistraction(uint16_t p2p, uint32_t nowMs);
