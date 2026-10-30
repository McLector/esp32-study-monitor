#include "sound.h"
#include "buzzer.h"
#include "session.h"

static uint16_t lo = 4095, hi = 0;  // running window since the last taskSound()
static uint16_t baseline = 0;
static bool anyDistraction = false;
static uint32_t lastDistractionMs = 0;

void soundInit() {
  lo = 4095; hi = 0;
  baseline = 0;
  anyDistraction = false;
  lastDistractionMs = 0;
}

void soundSetBaseline(uint16_t p2p) { baseline = p2p; }
uint16_t soundBaseline() { return baseline; }

// Average peak-to-peak over 25 ms windows for 2 s, nobody clapping.
void calibrateSound() {
  uint32_t sum = 0, windows = 0;
  const uint32_t start = millis();
  while (millis() - start < SOUND_CALIBRATE_MS) {
    uint16_t wlo = 4095, whi = 0;
    const uint32_t t0 = millis();
    while (millis() - t0 < SOUND_WINDOW_MS) {
      const uint16_t v = analogRead(PIN_SOUND_AO);
      if (v < wlo) wlo = v;
      if (v > whi) whi = v;
    }
    sum += whi > wlo ? whi - wlo : 0;
    windows++;
  }
  baseline = windows ? sum / windows : 0;
}

void soundSample() {  // every loop() pass
  const uint16_t v = analogRead(PIN_SOUND_AO);
  if (v < lo) lo = v;
  if (v > hi) hi = v;
}

void taskSound() {  // every 50 ms
  const uint16_t p2p = hi > lo ? hi - lo : 0;
  lo = 4095; hi = 0;
  if (buzzerActiveWithin(SOUND_MUTE_AFTER_BUZZ_MS)) return;  // our own beep is not a distraction
  if (p2p > app.live.noisePeak) app.live.noisePeak = p2p;   // V4 push resets it
  checkDistraction(p2p, millis());
}

bool checkDistraction(uint16_t p2p, uint32_t nowMs) {
  if (!app.live.present) return false;
  if (p2p <= baseline + app.cfg.noiseMargin) return false;
  if (anyDistraction && nowMs - lastDistractionMs < DISTRACTION_COOLDOWN_S * 1000UL) return false;
  anyDistraction = true;
  lastDistractionMs = nowMs;
  app.day.distractions++;
  app.ses.distractions++;
  onSignal(Signal::Distraction);
  return true;
}
