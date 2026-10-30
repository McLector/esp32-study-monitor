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
  if (L.sitSec >= sitLimitSec() && L.dueSec < BREAK_GRACE_MIN * MIN_S)
    app.day.breaksOnTime++;
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

// Phase 10 fills this in.
void tickEyeRest() {}

// Phase 9 fills this in.
int8_t computeFocus(const Daily&) { return -1; }

void resetToday() {
  const uint32_t ymd = app.day.ymd;  // still today: keep the date for rollover
  app.day = Daily{};
  app.day.ymd = ymd;
  const bool active = app.ses.active;
  app.ses = Session{};
  app.ses.active = active;
}
