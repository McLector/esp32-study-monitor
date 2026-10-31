#include "summary.h"

bool formatSummary(char* out, size_t n, const Session& s, int8_t focus) {
  if (n) out[0] = '\0';
  const uint32_t mins = s.studySec / MIN_S;
  if (mins < MIN_SUMMARY_MIN) return false;  // grabbing a pen is not a session
  // The final departure also completed a "break" in the state machine: drop it.
  const unsigned breaks = s.breaks > 0 ? s.breaks - 1u : 0u;
  const unsigned dist = s.distractions;
  char score[5];
  if (focus < 0) snprintf(score, sizeof score, "--");
  else snprintf(score, sizeof score, "%d", (int)focus);
  snprintf(out, n, "You studied %lu min, took %u break%s, had %u distraction%s. Focus today: %s.",
           (unsigned long)mins, breaks, breaks == 1 ? "" : "s", dist, dist == 1 ? "" : "s", score);
  return true;
}

bool queueSummary(char* pending, size_t n, const Session& s, int8_t focus) {
  char tmp[SUMMARY_LEN];
  if (!formatSummary(tmp, sizeof tmp, s, focus)) return false;
  snprintf(pending, n, "%s", tmp);
  return true;
}
