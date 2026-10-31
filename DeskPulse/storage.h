#pragma once
#include "state.h"

// Settings ranges, enforced on the device whatever the slider or flash says.
constexpr uint8_t SIT_LIMIT_MIN_LO = 10, SIT_LIMIT_MIN_HI = 60;
constexpr uint8_t BREAK_MIN_LO = 1, BREAK_MIN_HI = 15;
constexpr uint16_t NOISE_MARGIN_HI = 2000;

// Dashboard writes (cloud.ino). Each clamps the raw value before narrowing it,
// and returns true (and marks settings for the next save) only on a real change,
// so the re-sync after every reconnect costs no flash writes.
bool cfgSetMuted(int32_t raw);
bool cfgSetNoiseMargin(int32_t raw);
bool cfgSetSitLimit(int32_t raw);
bool cfgSetBreakMin(int32_t raw);
bool cfgSetEyeRest(int32_t raw);

void sanitizeSettings(Settings& s);  // clamp every field into range
bool sameDaily(const Daily& a, const Daily& b);  // field by field: padding bytes are junk

// NVS blobs. Loads return false (and leave the target untouched) when the key is
// missing, the size is wrong, or a Daily blob comes from a different build.
bool saveSettings(const Settings& s);
bool loadSettings(Settings& s);
bool saveDaily(const Daily& d);
bool loadDaily(Daily& d);

struct Restored { bool settings = false, daily = false; };
Restored storageLoad();  // boot: NVS -> app.cfg / app.day, then sets the save baseline

enum : uint8_t { SAVED_SETTINGS = 1, SAVED_DAILY = 2 };
uint8_t storagePersist();  // every 60 s: writes what changed, returns SAVED_* flags
