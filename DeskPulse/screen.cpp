#include "screen.h"
#include "session.h"

void formatMmss(char* out, size_t n, uint32_t sec) {
  if (sec > 99 * 60 + 59) sec = 99 * 60 + 59;
  snprintf(out, n, "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

static const char* titleFor(const Live& L) {
  switch (L.mode) {
    case Mode::Studying: return "STUDYING";
    case Mode::BreakDue: return "BREAK TIME";
    case Mode::OnBreak: return "ON BREAK";
    default: return L.objectSuspected ? "OBJECT?" : "AWAY";
  }
}

Screen buildScreen(bool wifiUp, bool cloudUp) {
  const Live& L = app.live;
  const uint32_t limit = sitLimitSec();
  const uint32_t brk = breakSec();
  Screen s;

  s.eyeRest = L.eyeRestLeft > 0;
  s.eyeLeft = L.eyeRestLeft;
  snprintf(s.title, sizeof s.title, "%s", titleFor(L));

  char t1[8], t2[8];
  if (L.mode == Mode::OnBreak) {
    formatMmss(t1, sizeof t1, L.awaySec < brk ? brk - L.awaySec : 0);
    snprintf(s.timers, sizeof s.timers, "Break counts in %s", t1);
  } else {
    formatMmss(t1, sizeof t1, L.sitSec);
    formatMmss(t2, sizeof t2, L.sitSec < limit ? limit - L.sitSec : 0);
    snprintf(s.timers, sizeof s.timers, "Sit %s  Left %s", t1, t2);
  }

  // Same fill as the LED bar; a zero limit counts as full (no divide by zero).
  s.barPx = (limit == 0 || L.sitSec >= limit)
                ? SCREEN_BAR_MAX_PX
                : (uint8_t)(SCREEN_BAR_MAX_PX * L.sitSec / limit);

  if (isnan(L.tempC) || isnan(L.humidity)) snprintf(s.env, sizeof s.env, "--.-C --%%RH");
  else snprintf(s.env, sizeof s.env, "%.1fC %.0f%%RH", L.tempC, L.humidity);

  snprintf(s.dist, sizeof s.dist, "D:%u", (unsigned)app.day.distractions);
  if (L.focus < 0) snprintf(s.focus, sizeof s.focus, "Focus --");
  else snprintf(s.focus, sizeof s.focus, "Focus %d", (int)L.focus);

  s.link = !wifiUp ? "Offline" : (cloudUp ? "Online" : "No cloud");

  // Burn-in protection: dim while away, panel off once the session has ended.
  s.dim = L.mode == Mode::Away;
  s.off = s.dim && !app.ses.active;
  return s;
}
