// Host-side tests for DeskPulse's logic modules. Build and run with run_tests.ps1.
// Assumes config.h has TEST_MODE = true (1 app minute = 1 s) and the 25/5 defaults.
#include <vector>
#include <DHT.h>  // the shim's fake sensor
#include "state.h"
#include "session.h"
#include "presence.h"
#include "sound.h"
#include "buzzer.h"
#include "env.h"
#include "screen.h"

AppState app;
static std::vector<Signal> signals;
void onSignal(Signal s) { signals.push_back(s); }

static int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) { ++failures; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define CHECK_EQ(a, b)                                                  \
  do {                                                                  \
    ++checks;                                                           \
    long long _a = (long long)(a), _b = (long long)(b);                 \
    if (_a != _b) { ++failures; printf("  FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } \
  } while (0)
#define CHECK_STR(a, b)                                                 \
  do {                                                                  \
    ++checks;                                                           \
    const char *_a = (a), *_b = (b);                                    \
    if (strcmp(_a, _b) != 0) { ++failures; printf("  FAIL %s:%d  %s == \"%s\"  (got \"%s\")\n", __FILE__, __LINE__, #a, _b, _a); } \
  } while (0)

static void resetAll() {
  fake::reset();
  fake::dhtTemp = fake::dhtHum = NAN;
  app = AppState{};
  signals.clear();
  presenceInit();
  soundInit();
  buzzerInit();
  sessionInit();
  envInit();
}

static int countSignal(Signal s) {
  int n = 0;
  for (Signal x : signals) n += x == s;
  return n;
}

static void ticks(int n) { for (int i = 0; i < n; i++) sessionTick(); }
static void sit(int n) { app.live.present = true; ticks(n); }
static void leave(int n) { app.live.present = false; ticks(n); }

// ---------------------------------------------------------------- presence

static void step(bool ir, bool pir) { presenceStep(ir, pir, fake::now); fake::now += 200; }

static void test_presence_needs_five_reads() {
  resetAll();
  for (int i = 0; i < 4; i++) step(true, true);
  CHECK(!app.live.present);
  step(true, true);
  CHECK(app.live.present);
}

static void test_presence_flicker_restarts_count() {
  resetAll();
  for (int i = 0; i < 3; i++) step(true, true);
  step(false, true);
  for (int i = 0; i < 4; i++) step(true, true);
  CHECK(!app.live.present);
  step(true, true);
  CHECK(app.live.present);
}

static void test_presence_leaving_needs_five_reads() {
  resetAll();
  for (int i = 0; i < 5; i++) step(true, true);
  for (int i = 0; i < 4; i++) step(false, false);
  CHECK(app.live.present);
  step(false, false);
  CHECK(!app.live.present);
}

static void test_presence_reads_pins_active_low() {
  resetAll();
  fake::pins[PIN_IR] = IR_ACTIVE_LOW ? LOW : HIGH;  // something in front
  fake::pins[PIN_PIR] = HIGH;
  for (int i = 0; i < 5; i++) { taskPresence(); fake::now += 200; }
  CHECK(app.live.present);
  fake::pins[PIN_IR] = IR_ACTIVE_LOW ? HIGH : LOW;  // idle
  for (int i = 0; i < 5; i++) { taskPresence(); fake::now += 200; }
  CHECK(!app.live.present);
}

static void test_object_guard_trips_without_motion() {
  resetAll();
  while (fake::now < OBJECT_GUARD_S * 1000UL) step(true, false);
  CHECK(app.live.present);
  CHECK(!app.live.objectSuspected);
  step(true, false);  // first read at the guard time
  CHECK(app.live.objectSuspected);
  CHECK(app.live.present);  // still debouncing
  for (int i = 0; i < 4; i++) step(true, false);
  CHECK(!app.live.present);
  CHECK(app.live.objectSuspected);
}

static void test_object_guard_clears_on_motion() {
  resetAll();
  while (fake::now < OBJECT_GUARD_S * 1000UL + 2000) step(true, false);
  CHECK(!app.live.present);
  step(true, true);  // PIR sees you move
  CHECK(!app.live.objectSuspected);
  for (int i = 0; i < 4; i++) step(true, false);
  CHECK(app.live.present);
}

static void test_object_guard_not_tripped_while_moving() {
  resetAll();
  while (fake::now < 2 * OBJECT_GUARD_S * 1000UL) step(true, true);
  CHECK(app.live.present);
  CHECK(!app.live.objectSuspected);
}

static void test_object_suspicion_clears_when_ir_idle() {
  resetAll();
  while (fake::now < OBJECT_GUARD_S * 1000UL + 200) step(true, false);
  CHECK(app.live.objectSuspected);
  step(false, false);
  CHECK(!app.live.objectSuspected);
}

// ---------------------------------------------------------------- session

static void test_sitting_starts_studying() {
  resetAll();
  sit(1);
  CHECK(app.live.mode == Mode::Studying);
  CHECK(app.ses.active);
  CHECK_EQ(app.live.sitSec, 1);
  CHECK_EQ(countSignal(Signal::ModeChanged), 1);
}

static void test_break_due_at_limit_once() {
  resetAll();
  sit(24);
  CHECK(app.live.mode == Mode::Studying);
  sit(1);
  CHECK(app.live.mode == Mode::BreakDue);
  CHECK_EQ(countSignal(Signal::BreakDue), 1);
  sit(10);
  CHECK(app.live.mode == Mode::BreakDue);
  CHECK_EQ(countSignal(Signal::BreakDue), 1);
}

static void test_skip_counted_once_after_grace() {
  resetAll();
  sit(25 + BREAK_GRACE_MIN - 2);
  CHECK_EQ(app.day.breaksSkipped, 0);
  sit(1);
  CHECK_EQ(app.day.breaksSkipped, 1);
  sit(30);
  CHECK_EQ(app.day.breaksSkipped, 1);
}

static void test_leaving_starts_on_break_then_counts() {
  resetAll();
  sit(10);
  leave(1);
  CHECK(app.live.mode == Mode::OnBreak);
  leave(3);
  CHECK(app.live.mode == Mode::OnBreak);
  CHECK_EQ(app.live.sitSec, 10);
  leave(1);  // full break length away
  CHECK(app.live.mode == Mode::Away);
  CHECK_EQ(app.live.sitSec, 0);
  CHECK_EQ(app.day.breaksTaken, 1);
  CHECK_EQ(app.ses.breaks, 1);
  CHECK_EQ(app.day.breaksOnTime, 0);  // left before the alert: not "on time"
}

static void test_early_return_keeps_sit_timer_and_alert() {
  resetAll();
  sit(26);
  leave(2);
  CHECK(app.live.mode == Mode::OnBreak);
  sit(1);
  CHECK(app.live.mode == Mode::BreakDue);
  CHECK_EQ(app.live.sitSec, 27);
  CHECK_EQ(countSignal(Signal::BreakDue), 1);
  CHECK_EQ(app.day.breaksTaken, 0);
}

static void test_early_return_from_studying() {
  resetAll();
  sit(10);
  leave(3);
  sit(1);
  CHECK(app.live.mode == Mode::Studying);
  CHECK_EQ(app.live.sitSec, 11);
  CHECK_EQ(app.live.awaySec, 0);
}

static void test_break_on_time_vs_late() {
  resetAll();
  sit(26);   // alert started 2 s ago
  leave(5);
  CHECK_EQ(app.day.breaksOnTime, 1);
  CHECK_EQ(app.day.breaksTaken, 1);

  sit(31);   // alert 7 s ago: skipped
  CHECK_EQ(app.day.breaksSkipped, 1);
  leave(5);
  CHECK_EQ(app.day.breaksOnTime, 1);
  CHECK_EQ(app.day.breaksTaken, 2);
  CHECK(!app.live.skipCounted);  // next cycle can count a skip again
}

static void test_session_ends_once_after_end_time() {
  resetAll();
  sit(10);
  leave(9);
  CHECK(app.ses.active);
  leave(1);
  CHECK(!app.ses.active);
  CHECK_EQ(countSignal(Signal::SessionEnded), 1);
  leave(20);
  CHECK_EQ(countSignal(Signal::SessionEnded), 1);
}

static void test_session_end_uses_longer_break() {
  resetAll();
  app.cfg.breakMin = 8;  // 8 + 5 = 13 > 10
  sit(10);
  leave(12);
  CHECK(app.ses.active);
  leave(1);
  CHECK(!app.ses.active);
}

static void test_study_counts_only_while_present() {
  resetAll();
  sit(7);
  leave(3);
  sit(2);
  CHECK_EQ(app.day.studySec, 9);
  CHECK_EQ(app.ses.studySec, 9);
}

static void test_new_session_starts_fresh() {
  resetAll();
  sit(10);
  leave(10);
  sit(1);
  CHECK(app.ses.active);
  CHECK_EQ(app.ses.studySec, 1);
  CHECK_EQ(app.ses.breaks, 0);
  CHECK_EQ(app.day.studySec, 11);  // the day keeps counting
}

static void test_lower_limit_triggers_break_next_tick() {
  resetAll();
  sit(15);
  app.cfg.sitLimitMin = 10;
  sit(1);
  CHECK(app.live.mode == Mode::BreakDue);
  CHECK_EQ(countSignal(Signal::BreakDue), 1);
}

static void test_catch_up_tick_after_stall() {
  resetAll();
  app.live.present = true;
  fake::now = 3000;
  taskTick();
  CHECK_EQ(app.live.sitSec, 3);
  fake::now = 3999;
  taskTick();
  CHECK_EQ(app.live.sitSec, 3);
  fake::now = 4000;
  taskTick();
  CHECK_EQ(app.live.sitSec, 4);
}

static void test_reset_today_keeps_settings() {
  resetAll();
  app.cfg.noiseMargin = 777;
  app.day.ymd = 20261031;
  sit(30);
  app.day.distractions = 4;
  app.ses.distractions = 4;
  resetToday();
  CHECK_EQ(app.day.studySec, 0);
  CHECK_EQ(app.day.distractions, 0);
  CHECK_EQ(app.day.breaksSkipped, 0);
  CHECK_EQ(app.ses.studySec, 0);
  CHECK_EQ(app.ses.distractions, 0);
  CHECK_EQ(app.day.ymd, 20261031);
  CHECK_EQ(app.cfg.noiseMargin, 777);
}

// ---------------------------------------------------------------- sound

static void test_no_distraction_while_away() {
  resetAll();
  soundSetBaseline(100);
  CHECK(!checkDistraction(4000, 10000));
  CHECK_EQ(app.day.distractions, 0);
}

static void test_distraction_threshold() {
  resetAll();
  soundSetBaseline(100);
  app.live.present = true;
  CHECK(!checkDistraction(100 + DEF_NOISE_MARGIN, 10000));
  CHECK(checkDistraction(100 + DEF_NOISE_MARGIN + 1, 10000));
  CHECK_EQ(app.day.distractions, 1);
  CHECK_EQ(app.ses.distractions, 1);
  CHECK_EQ(countSignal(Signal::Distraction), 1);
}

static void test_distraction_uses_live_margin() {
  resetAll();
  soundSetBaseline(100);
  app.live.present = true;
  app.cfg.noiseMargin = 1000;
  CHECK(!checkDistraction(900, 10000));
  CHECK(checkDistraction(1101, 10000));
}

static void test_distraction_cooldown() {
  resetAll();
  soundSetBaseline(100);
  app.live.present = true;
  CHECK(checkDistraction(2000, 10000));
  CHECK(!checkDistraction(2000, 12999));
  CHECK(checkDistraction(2000, 13000));
  CHECK_EQ(app.day.distractions, 2);
}

static void test_first_distraction_at_time_zero() {
  resetAll();
  soundSetBaseline(100);
  app.live.present = true;
  CHECK(checkDistraction(2000, 0));
}

static void loudWindow() {  // one 50 ms window with a 4000 peak-to-peak swing
  fake::analog[PIN_SOUND_AO] = 0;
  soundSample();
  fake::analog[PIN_SOUND_AO] = 4000;
  soundSample();
  taskSound();
}

static void test_own_beep_is_not_a_distraction() {
  resetAll();
  soundSetBaseline(100);
  app.live.present = true;
  fake::now = 10000;
  buzzerPlay(Pattern::BreakAlert);
  buzzerUpdate();
  loudWindow();
  CHECK_EQ(app.day.distractions, 0);
  CHECK_EQ(app.live.noisePeak, 0);
  fake::now = 10300;  // pattern over
  buzzerUpdate();
  CHECK(!buzzerIsOn());
  fake::now = 10500;  // 200 ms after: still muted
  loudWindow();
  CHECK_EQ(app.day.distractions, 0);
  fake::now = 10650;  // 350 ms after
  loudWindow();
  CHECK_EQ(app.day.distractions, 1);
}

static void test_sound_window_resets() {
  resetAll();
  soundSetBaseline(100);
  app.live.present = true;
  fake::now = 10000;
  loudWindow();
  CHECK_EQ(app.day.distractions, 1);
  fake::now = 20000;  // well past cooldown
  taskSound();        // no samples since the last window
  fake::analog[PIN_SOUND_AO] = 2000;
  soundSample();
  soundSample();
  taskSound();        // flat signal
  CHECK_EQ(app.day.distractions, 1);
}

static void test_noise_peak_tracks_max_even_when_away() {
  resetAll();
  soundSetBaseline(100);
  fake::analog[PIN_SOUND_AO] = 1000; soundSample();
  fake::analog[PIN_SOUND_AO] = 1500; soundSample();
  taskSound();
  fake::analog[PIN_SOUND_AO] = 1000; soundSample();
  fake::analog[PIN_SOUND_AO] = 1300; soundSample();
  taskSound();
  CHECK_EQ(app.live.noisePeak, 500);
  CHECK_EQ(app.day.distractions, 0);
}

// ---------------------------------------------------------------- buzzer and LEDs

static bool buzzerPinOn() { return fake::pins[PIN_BUZZER] == (BUZZER_ACTIVE_LOW ? LOW : HIGH); }

static void test_buzzer_init_is_silent() {
  resetAll();
  CHECK(!buzzerPinOn());
  CHECK_EQ(fake::modes[PIN_BUZZER], OUTPUT);
  fake::now = 100;
  CHECK(!buzzerActiveWithin(300));
}

static void test_break_alert_pattern() {
  resetAll();
  fake::now = 1000;
  buzzerPlay(Pattern::BreakAlert);
  buzzerUpdate();
  CHECK(buzzerIsOn());
  CHECK(buzzerPinOn());
  fake::now = 1100; buzzerUpdate();
  CHECK(!buzzerIsOn());
  CHECK(!buzzerPinOn());
  fake::now = 1200; buzzerUpdate();
  CHECK(buzzerIsOn());
  fake::now = 1300; buzzerUpdate();
  CHECK(!buzzerIsOn());
  fake::now = 2000; buzzerUpdate();
  CHECK(!buzzerIsOn());
  CHECK(buzzerActiveWithin(800));
  CHECK(!buzzerActiveWithin(700));
}

static void test_alert_repeats_every_period() {
  resetAll();
  app.live.mode = Mode::BreakDue;
  fake::now = 5000;
  alertUpdate(); buzzerUpdate();
  CHECK(buzzerIsOn());
  fake::now = 5400; alertUpdate(); buzzerUpdate();
  CHECK(!buzzerIsOn());
  fake::now = 5000 + ALERT_REPEAT_MS - 1; alertUpdate(); buzzerUpdate();
  CHECK(!buzzerIsOn());
  fake::now = 5000 + ALERT_REPEAT_MS; alertUpdate(); buzzerUpdate();
  CHECK(buzzerIsOn());
}

static void test_muted_alert_is_silent() {
  resetAll();
  app.live.mode = Mode::BreakDue;
  app.cfg.muted = true;
  for (uint32_t t = 0; t < 12000; t += 50) { fake::now = t; alertUpdate(); buzzerUpdate(); CHECK(!buzzerPinOn()); }
  CHECK_EQ(ledMask(Mode::BreakDue, 25, 25, 0), 0x1F);  // LEDs still blink
}

static void test_no_alert_outside_break_due() {
  resetAll();
  for (Mode m : {Mode::Away, Mode::Studying, Mode::OnBreak}) {
    app.live.mode = m;
    fake::now += 6000;
    alertUpdate(); buzzerUpdate();
    CHECK(!buzzerIsOn());
  }
}

static void test_alert_restarts_immediately_on_reentry() {
  resetAll();
  app.live.mode = Mode::BreakDue;
  fake::now = 0; alertUpdate(); buzzerUpdate();
  fake::now = 1000; app.live.mode = Mode::OnBreak; alertUpdate(); buzzerUpdate();
  CHECK(!buzzerIsOn());
  fake::now = 2000; app.live.mode = Mode::BreakDue; alertUpdate(); buzzerUpdate();
  CHECK(buzzerIsOn());
}

static void test_led_mask_progress() {
  CHECK_EQ(ledMask(Mode::Studying, 0, 25, 0), 0x01);
  CHECK_EQ(ledMask(Mode::Studying, 4, 25, 0), 0x01);
  CHECK_EQ(ledMask(Mode::Studying, 5, 25, 0), 0x03);
  CHECK_EQ(ledMask(Mode::Studying, 15, 25, 0), 0x0F);
  CHECK_EQ(ledMask(Mode::Studying, 20, 25, 0), 0x1F);
  CHECK_EQ(ledMask(Mode::Studying, 24, 25, 0), 0x1F);
  CHECK_EQ(ledMask(Mode::Studying, 40, 25, 0), 0x1F);  // never more than 5
  CHECK_EQ(ledMask(Mode::Studying, 3, 0, 0), 0x1F);    // zero limit: no divide by zero
  CHECK_EQ(ledMask(Mode::Away, 10, 25, 0), 0);
  CHECK_EQ(ledMask(Mode::OnBreak, 10, 25, 0), 0);
}

static void test_led_mask_blinks_in_break_due() {
  CHECK_EQ(ledMask(Mode::BreakDue, 25, 25, 0), 0x1F);
  CHECK_EQ(ledMask(Mode::BreakDue, 25, 25, 250), 0);
  CHECK_EQ(ledMask(Mode::BreakDue, 25, 25, 500), 0x1F);
}

static void test_leds_update_writes_pins() {
  resetAll();
  app.live.mode = Mode::Studying;
  app.live.sitSec = 5;  // limit 25 → 2 LEDs
  ledsUpdate();
  CHECK_EQ(fake::pins[PIN_LEDS[0]], HIGH);
  CHECK_EQ(fake::pins[PIN_LEDS[1]], HIGH);
  CHECK_EQ(fake::pins[PIN_LEDS[2]], LOW);
  CHECK_EQ(fake::pins[PIN_LEDS[4]], LOW);
  app.live.mode = Mode::Away;
  ledsUpdate();
  CHECK_EQ(fake::pins[PIN_LEDS[0]], LOW);
}

// ---------------------------------------------------------------- environment

static void test_env_keeps_last_good_reading() {
  resetAll();
  CHECK(std::isnan(app.live.tempC));
  envApply(25.5f, 60.0f);
  CHECK(app.live.tempC == 25.5f);
  CHECK(app.live.humidity == 60.0f);
  envApply(NAN, NAN);
  CHECK(app.live.tempC == 25.5f);
  envApply(26.0f, NAN);  // half a reading is a failed reading
  CHECK(app.live.tempC == 25.5f);
  CHECK(app.live.humidity == 60.0f);
}

static void test_env_task_reads_sensor() {
  resetAll();
  fake::dhtTemp = 29.0f;
  fake::dhtHum = 70.0f;
  taskEnvironment();
  CHECK(app.live.tempC == 29.0f);
  CHECK(app.live.humidity == 70.0f);
}

// ---------------------------------------------------------------- screen (phase 7)

static Screen screenNow(bool wifi = true, bool cloud = true) { return buildScreen(wifi, cloud); }

static void test_screen_mode_words() {
  resetAll();
  CHECK_STR(screenNow().title, "AWAY");
  app.live.objectSuspected = true;
  CHECK_STR(screenNow().title, "OBJECT?");
  app.live.mode = Mode::Studying;  // the guard word only replaces AWAY
  CHECK_STR(screenNow().title, "STUDYING");
  app.live.mode = Mode::BreakDue;
  CHECK_STR(screenNow().title, "BREAK TIME");
  app.live.mode = Mode::OnBreak;
  CHECK_STR(screenNow().title, "ON BREAK");
}

static void test_screen_sit_and_left_timers() {
  resetAll();
  CHECK_STR(screenNow().timers, "Sit 00:00  Left 00:25");
  sit(12);
  CHECK_STR(screenNow().timers, "Sit 00:12  Left 00:13");
}

static void test_screen_left_hits_zero_as_alert_starts() {
  resetAll();
  sit(24);
  CHECK_STR(screenNow().timers, "Sit 00:24  Left 00:01");
  CHECK(app.live.mode == Mode::Studying);
  sit(1);  // the tick that reaches the limit
  CHECK(app.live.mode == Mode::BreakDue);
  CHECK_STR(screenNow().timers, "Sit 00:25  Left 00:00");
  CHECK_EQ(screenNow().barPx, SCREEN_BAR_MAX_PX);
}

static void test_screen_left_never_underflows() {
  resetAll();
  sit(40);  // well past the limit, still seated
  CHECK_STR(screenNow().timers, "Sit 00:40  Left 00:00");
  CHECK_EQ(screenNow().barPx, SCREEN_BAR_MAX_PX);
}

static void test_screen_on_break_countdown_ends_when_break_counts() {
  resetAll();
  sit(10);
  leave(1);
  CHECK(app.live.mode == Mode::OnBreak);
  CHECK_STR(screenNow().timers, "Break counts in 00:04");
  leave(3);
  CHECK_STR(screenNow().timers, "Break counts in 00:01");
  CHECK(app.live.mode == Mode::OnBreak);
  leave(1);  // countdown would hit 00:00 here: the break is counted instead
  CHECK(app.live.mode == Mode::Away);
  CHECK_EQ(app.day.breaksTaken, 1);
}

static void test_screen_countdown_never_underflows() {
  resetAll();
  app.live.mode = Mode::OnBreak;
  app.live.awaySec = 99;  // past the break length
  CHECK_STR(screenNow().timers, "Break counts in 00:00");
}

static void test_mmss_formats_and_caps() {
  char b[8];
  formatMmss(b, sizeof b, 0);     CHECK_STR(b, "00:00");
  formatMmss(b, sizeof b, 59);    CHECK_STR(b, "00:59");
  formatMmss(b, sizeof b, 60);    CHECK_STR(b, "01:00");
  formatMmss(b, sizeof b, 3599);  CHECK_STR(b, "59:59");
  formatMmss(b, sizeof b, 5999);  CHECK_STR(b, "99:59");
  formatMmss(b, sizeof b, 6000);  CHECK_STR(b, "99:59");      // capped, not "100:00"
  formatMmss(b, sizeof b, 0xFFFFFFFFu); CHECK_STR(b, "99:59");
  char tiny[3];
  formatMmss(tiny, sizeof tiny, 75);  // too-small buffer: truncated, still terminated
  CHECK_EQ(strlen(tiny), 2);
}

static void test_screen_long_sit_fits_the_row() {
  resetAll();
  app.live.mode = Mode::BreakDue;
  app.live.sitSec = 2 * 3600;  // ignored the alert for two hours
  CHECK_STR(screenNow().timers, "Sit 99:59  Left 00:00");
  CHECK(strlen(screenNow().timers) <= 21);
}

static void test_screen_bar_scales_and_handles_zero_limit() {
  resetAll();
  CHECK_EQ(screenNow().barPx, 0);
  app.live.sitSec = 12;  // 12/25 of 126 px
  CHECK_EQ(screenNow().barPx, 126 * 12 / 25);
  app.cfg.sitLimitMin = 0;  // corrupt setting must not divide by zero
  CHECK_EQ(screenNow().barPx, SCREEN_BAR_MAX_PX);
  CHECK_STR(screenNow().timers, "Sit 00:12  Left 00:00");
}

static void test_screen_env_and_missing_reading() {
  resetAll();
  CHECK_STR(screenNow().env, "--.-C --%RH");  // no DHT reading yet
  app.live.tempC = 28.46f;
  app.live.humidity = 65.4f;
  CHECK_STR(screenNow().env, "28.5C 65%RH");
  app.live.humidity = NAN;  // half a reading still counts as missing
  CHECK_STR(screenNow().env, "--.-C --%RH");
}

static void test_screen_distractions_and_focus() {
  resetAll();
  CHECK_STR(screenNow().dist, "D:0");
  app.day.distractions = 65535;
  CHECK_STR(screenNow().dist, "D:65535");
  CHECK_STR(screenNow().focus, "Focus --");  // -1 = not enough data
  app.live.focus = 0;
  CHECK_STR(screenNow().focus, "Focus 0");
  app.live.focus = 100;
  CHECK_STR(screenNow().focus, "Focus 100");
}

static void test_screen_link_states() {
  resetAll();
  CHECK_STR(screenNow(true, true).link, "Online");
  CHECK_STR(screenNow(true, false).link, "No cloud");
  CHECK_STR(screenNow(false, false).link, "Offline");
  CHECK_STR(screenNow(false, true).link, "Offline");  // stale cloud flag without Wi-Fi
}

static void test_screen_eye_rest_overlay() {
  resetAll();
  CHECK(!screenNow().eyeRest);
  app.live.mode = Mode::Studying;
  app.live.eyeRestLeft = 20;
  Screen s = screenNow();
  CHECK(s.eyeRest);
  CHECK_EQ(s.eyeLeft, 20);
}

static void test_screen_burn_in_dim_and_off() {
  resetAll();
  CHECK(screenNow().off);  // boot: away, no session yet
  sit(3);
  Screen s = screenNow();
  CHECK(!s.dim);
  CHECK(!s.off);
  leave(1);  // on break: still a live screen
  CHECK(!screenNow().dim);
  leave(4);  // break counted: away, session still active
  CHECK(app.live.mode == Mode::Away);
  s = screenNow();
  CHECK(s.dim);
  CHECK(!s.off);
  leave(10);  // session ended
  CHECK(!app.ses.active);
  CHECK(screenNow().off);
  sit(1);  // wakes as soon as you sit down
  s = screenNow();
  CHECK(!s.off);
  CHECK(!s.dim);
}

static void test_screen_rows_fit_their_width() {
  resetAll();
  const Mode modes[] = {Mode::Away, Mode::Studying, Mode::BreakDue, Mode::OnBreak};
  for (Mode m : modes) {
    app.live.mode = m;
    app.live.objectSuspected = m == Mode::Away;
    Screen s = screenNow();
    CHECK(strlen(s.title) <= 10);
    CHECK(strlen(s.timers) <= 21);
  }
}

// ----------------------------------------------------------------

int main() {
  void (*tests[])() = {
    test_presence_needs_five_reads, test_presence_flicker_restarts_count,
    test_presence_leaving_needs_five_reads, test_presence_reads_pins_active_low,
    test_object_guard_trips_without_motion, test_object_guard_clears_on_motion,
    test_object_guard_not_tripped_while_moving, test_object_suspicion_clears_when_ir_idle,
    test_sitting_starts_studying, test_break_due_at_limit_once,
    test_skip_counted_once_after_grace, test_leaving_starts_on_break_then_counts,
    test_early_return_keeps_sit_timer_and_alert, test_early_return_from_studying,
    test_break_on_time_vs_late, test_session_ends_once_after_end_time,
    test_session_end_uses_longer_break, test_study_counts_only_while_present,
    test_new_session_starts_fresh, test_lower_limit_triggers_break_next_tick,
    test_catch_up_tick_after_stall, test_reset_today_keeps_settings,
    test_no_distraction_while_away, test_distraction_threshold,
    test_distraction_uses_live_margin, test_distraction_cooldown,
    test_first_distraction_at_time_zero, test_own_beep_is_not_a_distraction,
    test_sound_window_resets, test_noise_peak_tracks_max_even_when_away,
    test_buzzer_init_is_silent, test_break_alert_pattern,
    test_alert_repeats_every_period, test_muted_alert_is_silent,
    test_no_alert_outside_break_due, test_alert_restarts_immediately_on_reentry,
    test_led_mask_progress, test_led_mask_blinks_in_break_due,
    test_leds_update_writes_pins, test_env_keeps_last_good_reading,
    test_env_task_reads_sensor,
    test_screen_mode_words, test_screen_sit_and_left_timers,
    test_screen_left_hits_zero_as_alert_starts, test_screen_left_never_underflows,
    test_screen_on_break_countdown_ends_when_break_counts,
    test_screen_countdown_never_underflows, test_mmss_formats_and_caps,
    test_screen_long_sit_fits_the_row, test_screen_bar_scales_and_handles_zero_limit,
    test_screen_env_and_missing_reading, test_screen_distractions_and_focus,
    test_screen_link_states, test_screen_eye_rest_overlay,
    test_screen_burn_in_dim_and_off, test_screen_rows_fit_their_width,
  };
  for (auto t : tests) t();
  printf("%d checks, %d failures, %zu tests\n", checks, failures, sizeof tests / sizeof *tests);
  return failures ? 1 : 0;
}
