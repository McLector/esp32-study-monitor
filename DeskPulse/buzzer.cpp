#include "buzzer.h"
#include "session.h"

// Step durations in ms, alternating on/off and starting with on.
static const uint16_t BREAK_ALERT[] = {100, 100, 100};       // double beep
static const uint16_t CHIRP3[] = {40, 80, 40, 80, 40};       // lighter, for eye-rest

static const uint16_t* steps = nullptr;
static uint8_t stepCount = 0, stepIdx = 0;
static uint32_t stepStartMs = 0;
static bool on = false, everOn = false;
static uint32_t lastOffMs = 0;
static bool alerting = false;
static uint32_t lastAlertMs = 0;

static void setOutput(bool v) {
  if (on && !v) lastOffMs = millis();
  if (v) everOn = true;
  on = v;
  digitalWrite(PIN_BUZZER, v != BUZZER_ACTIVE_LOW ? HIGH : LOW);
}

void buzzerInit() {
  pinMode(PIN_BUZZER, OUTPUT);
  for (uint8_t p : PIN_LEDS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  steps = nullptr;
  stepCount = stepIdx = 0;
  on = everOn = alerting = false;
  lastOffMs = lastAlertMs = 0;
  setOutput(false);
}

void buzzerPlay(Pattern p) {
  if (p == Pattern::BreakAlert) { steps = BREAK_ALERT; stepCount = sizeof BREAK_ALERT / sizeof *BREAK_ALERT; }
  else { steps = CHIRP3; stepCount = sizeof CHIRP3 / sizeof *CHIRP3; }
  stepIdx = 0;
  stepStartMs = millis();
  setOutput(true);
}

void buzzerUpdate() {
  if (!steps) return;
  const uint32_t now = millis();
  while (steps && now - stepStartMs >= steps[stepIdx]) {
    stepStartMs += steps[stepIdx];
    if (++stepIdx >= stepCount) { steps = nullptr; setOutput(false); }
    else setOutput(stepIdx % 2 == 0);
  }
}

bool buzzerIsOn() { return on; }

bool buzzerActiveWithin(uint32_t ms) {
  if (on) return true;
  return everOn && millis() - lastOffMs < ms;
}

void alertUpdate() {
  if (app.live.mode != Mode::BreakDue) { alerting = false; return; }
  const uint32_t now = millis();
  if (alerting && now - lastAlertMs < ALERT_REPEAT_MS) return;
  alerting = true;
  lastAlertMs = now;
  if (!app.cfg.muted) buzzerPlay(Pattern::BreakAlert);
}

uint8_t ledMask(Mode m, uint32_t sitSec, uint32_t limitSec, uint32_t nowMs) {
  switch (m) {
    case Mode::BreakDue: return (nowMs / 250) % 2 ? 0 : 0x1F;  // 2 Hz blink
    case Mode::Studying: {
      if (limitSec == 0) return 0x1F;
      // Blue lights for 0-20 %, green joins at 20 %, … red at 80 %.
      uint32_t n = sitSec * 5 / limitSec + 1;
      if (n > 5) n = 5;
      return (1u << n) - 1;
    }
    default: return 0;  // Away, OnBreak
  }
}

void ledsUpdate() {
  const uint8_t mask = ledMask(app.live.mode, app.live.sitSec, sitLimitSec(), millis());
  for (uint8_t i = 0; i < 5; i++) digitalWrite(PIN_LEDS[i], (mask >> i) & 1 ? HIGH : LOW);
}
