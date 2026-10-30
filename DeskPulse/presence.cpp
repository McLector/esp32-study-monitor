#include "presence.h"

static uint8_t streak = 0;         // consecutive reads disagreeing with app.live.present
static uint32_t lastMotionMs = 0;  // object guard clock, real time

void presenceInit() {
  pinMode(PIN_IR, INPUT);  // module on 3V3 drives the pin: no internal pull-up
  pinMode(PIN_PIR, INPUT);
  streak = 0;
  lastMotionMs = millis();
}

void presenceStep(bool irActive, bool pirMotion, uint32_t nowMs) {
  Live& L = app.live;
  if (pirMotion) lastMotionMs = nowMs;
  // IR held without any PIR motion for the guard time: a bag, not a person.
  const bool guardTripped = USE_PIR_GUARD && nowMs - lastMotionMs >= OBJECT_GUARD_S * 1000UL;
  L.objectSuspected = irActive && guardTripped;
  const bool seen = irActive && !guardTripped;

  if (seen == L.present) { streak = 0; return; }
  if (++streak >= PRESENCE_DEBOUNCE_READS) {
    L.present = seen;
    streak = 0;
  }
}

void taskPresence() {
  const bool ir = digitalRead(PIN_IR) == (IR_ACTIVE_LOW ? LOW : HIGH);
  presenceStep(ir, digitalRead(PIN_PIR) == HIGH, millis());
}
