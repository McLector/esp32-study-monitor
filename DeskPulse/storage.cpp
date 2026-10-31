#include "storage.h"
#include <Preferences.h>

static Preferences prefs;
static const char* NVS_NS = "deskpulse";

static bool settingsDirty = false;  // saved by storagePersist, not on every slider tick
static Daily lastSavedDay;          // what flash holds, to skip unchanged saves

// Clamp in 32 bits, then narrow: storing 300 in a uint8_t first would give 44.
static int32_t clampI(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

template <class T>
static bool setField(T& field, T v) {
  if (field == v) return false;
  field = v;
  settingsDirty = true;
  return true;
}

bool cfgSetEyeRest(int32_t raw) { return setField(app.cfg.eyeRestOn, raw != 0); }
bool cfgSetMuted(int32_t raw) { return setField(app.cfg.muted, raw != 0); }
bool cfgSetNoiseMargin(int32_t raw) {
  return setField(app.cfg.noiseMargin, (uint16_t)clampI(raw, 0, NOISE_MARGIN_HI));
}
bool cfgSetSitLimit(int32_t raw) {
  return setField(app.cfg.sitLimitMin, (uint8_t)clampI(raw, SIT_LIMIT_MIN_LO, SIT_LIMIT_MIN_HI));
}
bool cfgSetBreakMin(int32_t raw) {
  return setField(app.cfg.breakMin, (uint8_t)clampI(raw, BREAK_MIN_LO, BREAK_MIN_HI));
}

void sanitizeSettings(Settings& s) {
  s.sitLimitMin = clampI(s.sitLimitMin, SIT_LIMIT_MIN_LO, SIT_LIMIT_MIN_HI);
  s.breakMin = clampI(s.breakMin, BREAK_MIN_LO, BREAK_MIN_HI);
  if (s.noiseMargin > NOISE_MARGIN_HI) s.noiseMargin = NOISE_MARGIN_HI;
}

bool sameDaily(const Daily& a, const Daily& b) {
  return a.version == b.version && a.ymd == b.ymd && a.studySec == b.studySec &&
         a.idleSec == b.idleSec && a.distractions == b.distractions &&
         a.breaksTaken == b.breaksTaken && a.breaksOnTime == b.breaksOnTime &&
         a.breaksSkipped == b.breaksSkipped;
}

static bool putBlob(const char* key, const void* p, size_t n) {
  if (!prefs.begin(NVS_NS, false)) return false;
  const bool ok = prefs.putBytes(key, p, n) == n;
  prefs.end();
  return ok;
}

static bool getBlob(const char* key, void* p, size_t n) {
  if (!prefs.begin(NVS_NS, true)) return false;  // first boot: namespace not created yet
  const bool ok = prefs.getBytes(key, p, n) == n;
  prefs.end();
  return ok;
}

bool saveSettings(const Settings& s) { return putBlob("cfg", &s, sizeof s); }

bool loadSettings(Settings& s) {
  Settings tmp;
  if (!getBlob("cfg", &tmp, sizeof tmp)) return false;  // defaults stay
  sanitizeSettings(tmp);  // never trust flash either
  s = tmp;
  return true;
}

bool saveDaily(const Daily& d) { return putBlob("day", &d, sizeof d); }

bool loadDaily(Daily& d) {
  Daily tmp;
  if (!getBlob("day", &tmp, sizeof tmp)) return false;
  if (tmp.version != Daily{}.version) return false;  // TEST_MODE and real builds never mix
  d = tmp;
  return true;
}

Restored storageLoad() {
  Restored r;
  r.settings = loadSettings(app.cfg);
  r.daily = loadDaily(app.day);
  settingsDirty = false;
  lastSavedDay = app.day;  // flash matches RAM now (or both are fresh)
  return r;
}

uint8_t storagePersist() {
  uint8_t saved = 0;
  if (settingsDirty && saveSettings(app.cfg)) {  // a failed write stays dirty: retried next time
    settingsDirty = false;
    saved |= SAVED_SETTINGS;
  }
  if (!sameDaily(app.day, lastSavedDay) && saveDaily(app.day)) {
    lastSavedDay = app.day;
    saved |= SAVED_DAILY;
  }
  return saved;
}
