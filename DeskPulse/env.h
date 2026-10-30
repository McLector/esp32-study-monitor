#pragma once
#include "state.h"

void envInit();
void envApply(float tempC, float humidity);  // keeps the last good reading on NaN
void taskEnvironment();                      // every 2.5 s: DHT11 read
