#pragma once
#include <Arduino.h>

// ---------- Pins ----------
constexpr uint8_t PIN_SDA = 21, PIN_SCL = 22;
constexpr uint8_t PIN_LEDS[5] = {25, 26, 27, 32, 33};  // blue → red
constexpr uint8_t PIN_BUZZER = 19;
constexpr uint8_t PIN_IR = 18;        // direct: IR module runs on 3V3, no divider
constexpr uint8_t PIN_PIR = 23;
constexpr uint8_t PIN_DHT = 16;
constexpr uint8_t PIN_SOUND_AO = 34;  // ADC1
constexpr uint8_t OLED_ADDR = 0x3C;   // some modules use 0x3D

// ---------- Feature flags ----------
constexpr bool TEST_MODE = true;
constexpr bool IR_ACTIVE_LOW = true;
constexpr bool USE_PIR_GUARD = true;
constexpr bool USE_MPU = false;
// Some 3-pin buzzer modules are low-level trigger. If yours buzzes at boot and
// goes quiet on a beep (phase 4 bench test), set this to true.
constexpr bool BUZZER_ACTIVE_LOW = false;

// One app "minute". In TEST_MODE it lasts 1 s: a 25-min Pomodoro takes 25 s.
constexpr uint32_t MIN_S = TEST_MODE ? 1 : 60;

// ---------- Defaults (Blynk sliders override, saved in NVS from phase 8) ----------
constexpr uint8_t DEF_SIT_LIMIT_MIN = 25;
constexpr uint8_t DEF_BREAK_MIN = 5;
constexpr uint16_t DEF_NOISE_MARGIN = 300;  // fallback until Blynk V6 syncs (tuned to 110 in 1.0)

// ---------- Fixed rules (in app minutes unless noted) ----------
constexpr uint8_t BREAK_GRACE_MIN = 5;       // still sitting this long after alert = skipped
constexpr uint8_t EYE_INTERVAL_MIN = 20;
constexpr uint8_t EYE_SKIP_WINDOW_MIN = 3;   // break due sooner than this: skip eye-rest
constexpr uint8_t SESSION_END_MIN = 10;      // away this long ends the session
constexpr uint8_t MIN_SUMMARY_MIN = 5;       // shorter sessions send no summary
constexpr uint16_t OBJECT_GUARD_S = TEST_MODE ? 60 : 300;  // REAL seconds, never scaled
constexpr uint8_t EYE_REST_SECONDS = TEST_MODE ? 5 : 20;   // real seconds
constexpr uint8_t DISTRACTION_COOLDOWN_S = 3;              // real seconds

// ---------- Timing details (real milliseconds) ----------
constexpr uint8_t PRESENCE_DEBOUNCE_READS = 5;   // 5 reads at 200 ms = 1 s
constexpr uint32_t ALERT_REPEAT_MS = 5000;       // break alert double-beep period
constexpr uint32_t SOUND_MUTE_AFTER_BUZZ_MS = 300;
constexpr uint32_t SOUND_CALIBRATE_MS = 2000;    // ambient baseline at boot, as in 1.0
constexpr uint32_t SOUND_WINDOW_MS = 25;         // calibration window, matches the bench sketch
