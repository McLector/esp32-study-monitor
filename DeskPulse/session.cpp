#include "session.h"

static uint32_t lastTickMs = 0;

uint32_t sitLimitSec() { return app.cfg.sitLimitMin * MIN_S; }
uint32_t breakSec() { return app.cfg.breakMin * MIN_S; }

static uint32_t sessionEndSec() {
  uint32_t m = app.cfg.breakMin + 5;
  if (m < SESSION_END_MIN) m = SESSION_END_MIN;
  return m * MIN_S;
}

static void setMode(Mode m) {
  if (app.live.mode == m) return;
  app.live.mode = m;
  onSignal(Signal::ModeChanged);
}

static void completeBreak() {
  Live& L = app.live;
  // On time = the alert fired and you left within the grace window. dueSec > 0
  // only while this cycle's alert stands, so a limit changed mid-break can't flip it.
  if (L.dueSec > 0 && L.dueSec < BREAK_GRACE_MIN * MIN_S) app.day.breaksOnTime++;
  app.day.breaksTaken++;
  app.ses.breaks++;
  L.sitSec = L.dueSec = L.eyeSec = 0;
  L.skipCounted = false;
  setMode(Mode::Away);
}

void sessionInit() { lastTickMs = millis(); }

void sessionTick() {
  Live& L = app.live;
  if (L.present) {
    L.awaySec = 0;
    if (!app.ses.active) { app.ses = Session{}; app.ses.active = true; }
    if (L.mode == Mode::Away || L.mode == Mode::OnBreak)  // back at the desk
      setMode(L.sitSec >= sitLimitSec() ? Mode::BreakDue : Mode::Studying);
    L.sitSec++; app.day.studySec++; app.ses.studySec++;
    tickEyeRest();  // Enhancement 3
    // Sit limit raised past the sit time (V11): the alert no longer applies.
    if (L.mode == Mode::BreakDue && L.sitSec < sitLimitSec()) setMode(Mode::Studying);
    if (L.mode == Mode::Studying) { L.dueSec = 0; L.skipCounted = false; }  // a counted skip stays counted
    if (L.mode == Mode::Studying && L.sitSec >= sitLimitSec()) {
      setMode(Mode::BreakDue);
      onSignal(Signal::BreakDue);  // once per cycle
    }
    if (L.mode == Mode::BreakDue) {
      L.dueSec++;
      if (!L.skipCounted && L.dueSec >= BREAK_GRACE_MIN * MIN_S) {
        app.day.breaksSkipped++;
        L.skipCounted = true;
      }
    }
  } else {
    L.awaySec++;
    L.eyeRestLeft = 0;  // left the desk: overlay off
    if (L.mode == Mode::Studying || L.mode == Mode::BreakDue) setMode(Mode::OnBreak);
    if (L.mode == Mode::OnBreak && L.awaySec >= breakSec()) completeBreak();
    if (app.ses.active && L.awaySec >= sessionEndSec()) {
      app.ses.active = false;
      onSignal(Signal::SessionEnded);  // Enhancement 4
    }
  }
  L.focus = computeFocus(app.day);
}

void taskTick() {
  while (millis() - lastTickMs >= 1000) {  // runs extra ticks after a stall
    lastTickMs += 1000;
    sessionTick();
  }
}

// Called from sessionTick() while present.
void tickEyeRest() {
  Live& L = app.live;
  if (L.eyeRestLeft > 0) { L.eyeRestLeft--; return; }  // countdown running
  if (!app.cfg.eyeRestOn || L.mode == Mode::BreakDue) return;
  if (++L.eyeSec < EYE_INTERVAL_MIN * MIN_S) return;
  L.eyeSec = 0;
  if (L.sitSec + EYE_SKIP_WINDOW_MIN * MIN_S >= sitLimitSec()) return;  // break is close
  L.eyeRestLeft = EYE_REST_SECONDS;
  onSignal(Signal::EyeRest);
}

// F = max(0, 100 - min(45, 15 S) - min(40, 4 R) - P_idle), R = 60 D / max(T, 30).
// The weights are design choices, kept here so tuning is a one-line change.
int8_t computeFocus(const Daily& d) {
  const float studyMin = d.studySec / (float)MIN_S;
  if (studyMin < 10) return -1;  // not enough data
  const float perHour = d.distractions * 60.0f / (studyMin < 30 ? 30.0f : studyMin);
  float penalty = fminf(45.0f, 15.0f * d.breaksSkipped) + fminf(40.0f, 4.0f * perHour);
  if (USE_MPU && d.studySec > 0) penalty += fminf(15.0f, 15.0f * d.idleSec / (float)d.studySec);
  const long score = lroundf(100.0f - penalty);
  return (int8_t)(score < 0 ? 0 : score);
}

bool dayRollover(uint32_t ymd) {
  if (ymd == 0) return false;                                // no NTP time yet
  if (app.day.ymd == 0) { app.day.ymd = ymd; return false; } // first sync: adopt (also restored counters)
  if (ymd == app.day.ymd) return false;                      // same day, also after a reboot
  onSignal(Signal::DayRolled);                               // final score goes to V15
  app.day = Daily{};
  app.day.ymd = ymd;
  return true;
}

void resetToday() {
  const uint32_t ymd = app.day.ymd;  // still today: keep the date for rollover
  app.day = Daily{};
  app.day.ymd = ymd;
  const bool active = app.ses.active;
  app.ses = Session{};
  app.ses.active = active;
}
