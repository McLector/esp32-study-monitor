// Blynk layer: the only place besides DeskPulse.ino that touches Blynk.
// Phase 6 covers 1.0's datastreams (V0-V9) and the break_time event; phase 8
// adds the V11/V12 Pomodoro sliders, phase 9 the focus score (V10, V15), phase 10
// eye-rest (V13, eye_rest) and phase 11 session_summary. V14 shows the mode.
// Settings logic lives in storage.cpp, the summary text in summary.cpp.

// Last value sent per pin. Plain ints, not a struct: the IDE's auto-generated
// prototypes land above any type declared in an .ino tab.
constexpr int32_t UNSENT = INT32_MIN;
static int32_t cV0 = UNSENT, cV1 = UNSENT, cV7 = UNSENT, cV8 = UNSENT, cV10 = UNSENT;
static char pendingSummary[SUMMARY_LEN] = "";  // waits here while offline; only the latest is kept
static float lastTemp = NAN, lastHum = NAN;

static void pushInt(int pin, int32_t v, int32_t& last) {
  if (!Blynk.connected() || v == last) return;  // unchanged values cost nothing
  Blynk.virtualWrite(pin, v);
  last = v;
}

static int32_t studyMinutes() { return app.day.studySec / MIN_S; }

static int32_t sitProgress() {
  const uint32_t limit = sitLimitSec();
  if (!limit) return 100;
  const uint32_t pct = app.live.sitSec * 100 / limit;
  return pct > 100 ? 100 : pct;
}

static void pushEnvironment(bool force) {
  if (!Blynk.connected() || isnan(app.live.tempC)) return;
  if (force || isnan(lastTemp) || fabsf(app.live.tempC - lastTemp) >= 0.5f) {
    Blynk.virtualWrite(V2, app.live.tempC);
    lastTemp = app.live.tempC;
  }
  if (force || isnan(lastHum) || fabsf(app.live.humidity - lastHum) >= 2.0f) {
    Blynk.virtualWrite(V3, app.live.humidity);
    lastHum = app.live.humidity;
  }
}

static void pushNoisePeak() {  // the window's maximum, then start a new window
  if (Blynk.connected()) Blynk.virtualWrite(V4, app.live.noisePeak);
  app.live.noisePeak = 0;
}

static void pushFocus() { pushInt(V10, focusGaugeValue(app.live.focus), cV10); }

static void pushStatus() {
  if (Blynk.connected()) Blynk.virtualWrite(V14, modeName(app.live.mode));
}

void flushSummary() {
  if (pendingSummary[0] && Blynk.connected()) {
    Blynk.logEvent("session_summary", pendingSummary);
    pendingSummary[0] = '\0';
  }
}

void pushAll() {  // after (re)connect: clear the cache, send everything once
  cV0 = cV1 = cV7 = cV8 = cV10 = UNSENT;
  pushInt(V0, studyMinutes(), cV0);
  pushInt(V1, app.day.distractions, cV1);
  pushInt(V7, app.live.present, cV7);
  pushInt(V8, sitProgress(), cV8);
  pushEnvironment(true);
  pushFocus();
  pushStatus();
}

void taskCloud() {  // every 30 s
  static bool minuteMark = false;
  minuteMark = !minuteMark;
  pushNoisePeak();
  pushInt(V8, sitProgress(), cV8);
  pushInt(V1, app.day.distractions, cV1);  // catches changes made while offline
  pushInt(V7, app.live.present, cV7);
  if (minuteMark) {  // every 60 s
    pushInt(V0, studyMinutes(), cV0);
    pushEnvironment(false);
    pushFocus();
  }
}

void cloudOnSignal(Signal s) {
  switch (s) {
    case Signal::ModeChanged:  // a few per hour: keep the dashboard in step at transitions
      pushInt(V7, app.live.present, cV7);
      pushInt(V8, sitProgress(), cV8);  // 100 % at Break due, 0 % once the break counts
      pushStatus();
      break;
    case Signal::Distraction: pushInt(V1, app.day.distractions, cV1); break;
    case Signal::BreakDue:
      if (Blynk.connected())
        Blynk.logEvent("break_time", "Sit limit reached. Stand up and take a break.");
      break;
    case Signal::EyeRest:
      if (Blynk.connected())
        Blynk.logEvent("eye_rest", "Look at something 20 ft (6 m) away for 20 seconds");
      break;
    case Signal::SessionEnded:
      if (queueSummary(pendingSummary, sizeof pendingSummary, app.ses, app.live.focus)) {
        Serial.printf(">> summary: %s\n", pendingSummary);
        flushSummary();
      }
      break;
    case Signal::DayRolled: {  // before the reset: today's final score
      const int8_t f = computeFocus(app.day);
      if (f >= 0 && Blynk.connected()) Blynk.virtualWrite(V15, f);
      break;
    }
    default: break;
  }
}

BLYNK_WRITE(V5) { cfgSetMuted(param.asInt()); }
BLYNK_WRITE(V6) { cfgSetNoiseMargin(param.asInt()); }
BLYNK_WRITE(V11) {
  if (cfgSetSitLimit(param.asInt())) Serial.printf(">> sit limit %u min\n", app.cfg.sitLimitMin);
}
BLYNK_WRITE(V12) {
  if (cfgSetBreakMin(param.asInt())) Serial.printf(">> break length %u min\n", app.cfg.breakMin);
}
BLYNK_WRITE(V13) { cfgSetEyeRest(param.asInt()); }
BLYNK_WRITE(V9) {
  if (param.asInt() == 1) {  // zero today's counters, keep settings
    resetToday();
    pushAll();
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V5, V6, V11, V12, V13);  // pull dashboard settings
  pushAll();                                 // one full refresh after (re)connect
  flushSummary();                            // a summary made while offline
}
