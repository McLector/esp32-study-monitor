// Host-side tests for DeskPulse's logic modules. Build and run with run_tests.ps1.
// Assumes config.h has TEST_MODE = true (1 app minute = 1 s) and the 25/5 defaults.
#include <vector>
#include <DHT.h>  // the shim's fake sensor
#include <Preferences.h>  // the shim's in-memory NVS
#include "state.h"
#include "session.h"
#include "presence.h"
#include "sound.h"
#include "buzzer.h"
#include "env.h"
#include "screen.h"
#include "storage.h"
#include "summary.h"

AppState app;
static std::vector<Signal> signals;
static Daily dayAtSignal;  // today's counters as onSignal(DayRolled) saw them
void onSignal(Signal s) {
  if (s == Signal::DayRolled) dayAtSignal = app.day;
  signals.push_back(s);
}

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
  fake::nvsReset();
  storageLoad();  // empty flash: defaults, and a clean save baseline
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

// Seated and in a study block: the only time noise counts as a distraction.
static void studying() { app.live.present = true; app.live.mode = Mode::Studying; }

static void test_no_distraction_while_away() {
  resetAll();
  soundSetBaseline(100);
  CHECK(!checkDistraction(4000, 10000));
  CHECK_EQ(app.day.distractions, 0);
}

static void test_distraction_threshold() {
  resetAll();
  soundSetBaseline(100);
  studying();
  CHECK(!checkDistraction(100 + DEF_NOISE_MARGIN, 10000));
  CHECK(checkDistraction(100 + DEF_NOISE_MARGIN + 1, 10000));
  CHECK_EQ(app.day.distractions, 1);
  CHECK_EQ(app.ses.distractions, 1);
  CHECK_EQ(countSignal(Signal::Distraction), 1);
}

static void test_distraction_uses_live_margin() {
  resetAll();
  soundSetBaseline(100);
  studying();
  app.cfg.noiseMargin = 1000;
  CHECK(!checkDistraction(900, 10000));
  CHECK(checkDistraction(1101, 10000));
}

static void test_distraction_cooldown() {
  resetAll();
  soundSetBaseline(100);
  studying();
  CHECK(checkDistraction(2000, 10000));
  CHECK(!checkDistraction(2000, 12999));
  CHECK(checkDistraction(2000, 13000));
  CHECK_EQ(app.day.distractions, 2);
}

static void test_first_distraction_at_time_zero() {
  resetAll();
  soundSetBaseline(100);
  studying();
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
  studying();
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

static void test_no_distraction_in_break_time() {
  resetAll();
  soundSetBaseline(100);
  sit(26);  // BREAK TIME: the study block is over, even while still seated
  CHECK(app.live.mode == Mode::BreakDue);
  CHECK(!checkDistraction(4000, 10000));
  leave(1);  // ON BREAK
  CHECK(!checkDistraction(4000, 20000));
  sit(1);    // back early: still BREAK TIME
  CHECK(app.live.mode == Mode::BreakDue);
  CHECK(!checkDistraction(4000, 30000));
  CHECK_EQ(app.day.distractions, 0);
  CHECK_EQ(app.ses.distractions, 0);
  CHECK_EQ(countSignal(Signal::Distraction), 0);
}

static void test_distraction_counts_again_after_the_break() {
  resetAll();
  soundSetBaseline(100);
  sit(26);
  leave(5);  // break taken
  sit(1);    // a new study block
  CHECK(app.live.mode == Mode::Studying);
  CHECK(checkDistraction(4000, 10000));
  CHECK_EQ(app.ses.distractions, 1);
}

static void test_sound_window_resets() {
  resetAll();
  soundSetBaseline(100);
  studying();
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

// ---------------------------------------------------------------- settings + storage (phase 8)

static void test_setters_clamp_before_narrowing() {
  resetAll();
  cfgSetSitLimit(300);  // 300 squeezed into a uint8_t first would be 44
  CHECK_EQ(app.cfg.sitLimitMin, 60);
  cfgSetSitLimit(-5);
  CHECK_EQ(app.cfg.sitLimitMin, 10);
  cfgSetSitLimit(0);  // what a never-set datastream may sync as
  CHECK_EQ(app.cfg.sitLimitMin, 10);
  cfgSetSitLimit(35);
  CHECK_EQ(app.cfg.sitLimitMin, 35);
  cfgSetSitLimit(INT32_MIN);
  CHECK_EQ(app.cfg.sitLimitMin, 10);
  cfgSetSitLimit(INT32_MAX);
  CHECK_EQ(app.cfg.sitLimitMin, 60);

  cfgSetBreakMin(0);
  CHECK_EQ(app.cfg.breakMin, 1);
  cfgSetBreakMin(270);  // 270 as a uint8_t is 14, which would pass the clamp
  CHECK_EQ(app.cfg.breakMin, 15);
  cfgSetBreakMin(16);
  CHECK_EQ(app.cfg.breakMin, 15);
  cfgSetBreakMin(7);
  CHECK_EQ(app.cfg.breakMin, 7);

  cfgSetNoiseMargin(70000);  // 70000 as a uint16_t is 4464
  CHECK_EQ(app.cfg.noiseMargin, 2000);
  cfgSetNoiseMargin(-1);
  CHECK_EQ(app.cfg.noiseMargin, 0);
  cfgSetNoiseMargin(110);
  CHECK_EQ(app.cfg.noiseMargin, 110);

  cfgSetMuted(5);
  CHECK(app.cfg.muted);
  cfgSetMuted(0);
  CHECK(!app.cfg.muted);
}

static void test_setters_report_real_changes_only() {
  resetAll();
  CHECK(!cfgSetSitLimit(25));  // same as the default
  CHECK(cfgSetSitLimit(30));
  CHECK(!cfgSetSitLimit(30));
  CHECK(cfgSetSitLimit(300));  // becomes 60
  CHECK(!cfgSetSitLimit(61));  // still 60
  CHECK(!cfgSetBreakMin(5));
  CHECK(cfgSetBreakMin(10));
  CHECK(!cfgSetMuted(0));
  CHECK(cfgSetMuted(1));
  CHECK(!cfgSetMuted(2));  // still muted
  CHECK(!cfgSetNoiseMargin(DEF_NOISE_MARGIN));
  CHECK(cfgSetNoiseMargin(110));
}

static void test_first_boot_keeps_defaults() {
  resetAll();
  app.cfg.sitLimitMin = 42;  // whatever is in RAM must survive a failed load
  app.day.distractions = 3;
  Restored r = storageLoad();
  CHECK(!r.settings);
  CHECK(!r.daily);
  CHECK_EQ(app.cfg.sitLimitMin, 42);
  CHECK_EQ(app.day.distractions, 3);
  Settings s;
  CHECK(!loadSettings(s));
  CHECK_EQ(s.sitLimitMin, DEF_SIT_LIMIT_MIN);
}

static void test_settings_survive_a_reboot() {
  resetAll();
  cfgSetSitLimit(30);
  cfgSetBreakMin(10);
  cfgSetNoiseMargin(110);
  cfgSetMuted(1);
  CHECK_EQ(storagePersist() & SAVED_SETTINGS, SAVED_SETTINGS);
  app = AppState{};  // reboot: RAM is gone, flash is not
  Restored r = storageLoad();
  CHECK(r.settings);
  CHECK_EQ(app.cfg.sitLimitMin, 30);
  CHECK_EQ(app.cfg.breakMin, 10);
  CHECK_EQ(app.cfg.noiseMargin, 110);
  CHECK(app.cfg.muted);
  CHECK_EQ(sitLimitSec(), 30);  // the state machine uses it straight away
}

static void test_counters_survive_a_reboot() {
  resetAll();
  sit(31);   // one skip
  leave(5);  // one break
  app.day.distractions = 4;
  CHECK_EQ(storagePersist() & SAVED_DAILY, SAVED_DAILY);
  const Daily before = app.day;
  app = AppState{};
  Restored r = storageLoad();
  CHECK(r.daily);
  CHECK(sameDaily(app.day, before));
  CHECK_EQ(app.day.studySec, 31);
  CHECK_EQ(app.day.distractions, 4);
  CHECK_EQ(app.day.breaksSkipped, 1);
  CHECK_EQ(app.day.breaksTaken, 1);
  CHECK(app.live.mode == Mode::Away);  // live state is never restored
  CHECK(!app.ses.active);
}

static void test_wrong_size_blob_is_rejected() {
  resetAll();
  const uint8_t shortBlob[3] = {30, 10, 1};
  fake::nvsPut("deskpulse", "cfg", shortBlob, sizeof shortBlob);
  Settings s;
  CHECK(!loadSettings(s));
  CHECK_EQ(s.sitLimitMin, DEF_SIT_LIMIT_MIN);

  uint8_t longBlob[sizeof(Settings) + 4] = {};
  fake::nvsPut("deskpulse", "cfg", longBlob, sizeof longBlob);
  CHECK(!loadSettings(s));

  uint8_t shortDay[sizeof(Daily) - 4] = {};
  shortDay[0] = Daily{}.version;
  fake::nvsPut("deskpulse", "day", shortDay, sizeof shortDay);
  Daily d;
  d.distractions = 9;
  CHECK(!loadDaily(d));
  CHECK_EQ(d.distractions, 9);
}

static void test_daily_from_other_build_is_rejected() {
  resetAll();
  Daily other;
  other.version = TEST_MODE ? 1 : 101;  // a real build's blob seen by a test build, or vice versa
  other.studySec = 5000;
  fake::nvsPut("deskpulse", "day", &other, sizeof other);
  Restored r = storageLoad();
  CHECK(!r.daily);
  CHECK_EQ(app.day.studySec, 0);
}

static void test_corrupt_settings_are_clamped_on_load() {
  resetAll();
  Settings bad;
  bad.sitLimitMin = 0;  // would mean BREAK TIME on every boot
  bad.breakMin = 200;
  bad.noiseMargin = 60000;
  fake::nvsPut("deskpulse", "cfg", &bad, sizeof bad);
  CHECK(storageLoad().settings);
  CHECK_EQ(app.cfg.sitLimitMin, 10);
  CHECK_EQ(app.cfg.breakMin, 15);
  CHECK_EQ(app.cfg.noiseMargin, 2000);

  bad.sitLimitMin = 255;
  bad.breakMin = 0;
  fake::nvsPut("deskpulse", "cfg", &bad, sizeof bad);
  CHECK(storageLoad().settings);
  CHECK_EQ(app.cfg.sitLimitMin, 60);
  CHECK_EQ(app.cfg.breakMin, 1);
}

static void test_persist_writes_only_what_changed() {
  resetAll();
  CHECK_EQ(storagePersist(), 0);  // nothing changed since boot
  CHECK_EQ(fake::nvsWrites, 0);
  sit(10);
  CHECK_EQ(storagePersist(), SAVED_DAILY);
  CHECK_EQ(storagePersist(), 0);
  CHECK_EQ(fake::nvsWrites, 1);
  cfgSetSitLimit(30);
  CHECK_EQ(storagePersist(), SAVED_SETTINGS);
  CHECK_EQ(fake::nvsWrites, 2);
}

static void test_reconnect_resync_costs_no_writes() {
  resetAll();
  cfgSetSitLimit(30);
  storagePersist();
  const int writes = fake::nvsWrites;
  // BLYNK_CONNECTED -> syncVirtual re-sends what the dashboard already holds
  cfgSetMuted(0);
  cfgSetNoiseMargin(DEF_NOISE_MARGIN);
  cfgSetSitLimit(30);
  cfgSetBreakMin(DEF_BREAK_MIN);
  CHECK_EQ(storagePersist(), 0);
  CHECK_EQ(fake::nvsWrites, writes);
}

static void test_slider_drag_is_one_write() {
  resetAll();
  for (int v = 10; v <= 60; v += 5) cfgSetSitLimit(v);  // many writes while dragging
  CHECK_EQ(storagePersist(), SAVED_SETTINGS);
  CHECK_EQ(fake::nvsWrites, 1);
  Settings s;
  CHECK(loadSettings(s));
  CHECK_EQ(s.sitLimitMin, 60);
}

static void test_failed_write_is_retried() {
  resetAll();
  cfgSetSitLimit(40);
  sit(3);
  fake::nvsFailWrites = true;
  CHECK_EQ(storagePersist(), 0);
  fake::nvsFailWrites = false;
  CHECK_EQ(storagePersist(), SAVED_SETTINGS | SAVED_DAILY);
  CHECK_EQ(storagePersist(), 0);
}

static void test_reset_today_is_saved() {
  resetAll();
  sit(20);
  storagePersist();
  resetToday();  // V9
  CHECK_EQ(storagePersist(), SAVED_DAILY);
  app = AppState{};
  storageLoad();
  CHECK_EQ(app.day.studySec, 0);
}

static void test_same_daily_ignores_padding() {
  Daily a;
  memset(static_cast<void*>(&a), 0, sizeof a);  // known padding: zeros
  a.version = Daily{}.version;
  a.ymd = 20261031;
  a.studySec = 100;
  a.distractions = 2;
  Daily b;
  memset(static_cast<void*>(&b), 0x5A, sizeof b);  // junk in every byte, padding included
  b.version = a.version; b.ymd = a.ymd; b.studySec = a.studySec; b.idleSec = a.idleSec;
  b.distractions = a.distractions; b.breaksTaken = a.breaksTaken;
  b.breaksOnTime = a.breaksOnTime; b.breaksSkipped = a.breaksSkipped;
  CHECK(memcmp(&a, &b, sizeof a) != 0);  // same fields, but a byte compare calls them different
  CHECK(sameDaily(a, b));
  b.breaksSkipped++;
  CHECK(!sameDaily(a, b));
  b.breaksSkipped--;
  b.idleSec++;
  CHECK(!sameDaily(a, b));
  b.idleSec--;
  b.ymd++;
  CHECK(!sameDaily(a, b));
}

// ---------------------------------------------------------------- mid-session changes (phase 8)

static void test_slider_change_shows_on_screen_at_once() {
  resetAll();
  sit(5);
  cfgSetSitLimit(30);
  CHECK_STR(screenNow().timers, "Sit 00:05  Left 00:25");
  sit(10);
  leave(1);
  cfgSetBreakMin(10);
  CHECK_STR(screenNow().timers, "Break counts in 00:09");
}

static void test_shorter_break_completes_next_tick() {
  resetAll();
  sit(10);
  leave(3);
  CHECK(app.live.mode == Mode::OnBreak);
  cfgSetBreakMin(2);  // already away longer than the new length
  leave(1);
  CHECK(app.live.mode == Mode::Away);
  CHECK_EQ(app.day.breaksTaken, 1);
}

static void test_raised_limit_during_break_due_resumes_studying() {
  resetAll();
  sit(27);  // BREAK TIME for 3 s
  CHECK(app.live.mode == Mode::BreakDue);
  cfgSetSitLimit(40);
  sit(1);
  CHECK(app.live.mode == Mode::Studying);  // no alert while time is left
  CHECK_EQ(app.live.dueSec, 0);
  CHECK_EQ(countSignal(Signal::BreakDue), 1);
  CHECK_STR(screenNow().timers, "Sit 00:28  Left 00:12");
  sit(12);  // reaches the new limit: warns again, once
  CHECK(app.live.mode == Mode::BreakDue);
  CHECK_EQ(countSignal(Signal::BreakDue), 2);
}

static void test_raised_limit_keeps_a_counted_skip() {
  resetAll();
  sit(31);
  CHECK_EQ(app.day.breaksSkipped, 1);
  cfgSetSitLimit(50);
  sit(1);
  CHECK(app.live.mode == Mode::Studying);
  CHECK_EQ(app.day.breaksSkipped, 1);  // not taken back
  CHECK(!app.live.skipCounted);        // the new alert can count its own skip
  sit(18 + BREAK_GRACE_MIN);           // new alert at 50; its skip counts at 54
  CHECK_EQ(app.day.breaksSkipped, 2);
}

static void test_raised_limit_during_on_break_keeps_on_time() {
  resetAll();
  sit(26);  // alert 2 s ago
  leave(1);
  cfgSetSitLimit(40);
  leave(4);  // full break
  CHECK_EQ(app.day.breaksTaken, 1);
  CHECK_EQ(app.day.breaksOnTime, 1);  // judged when the alert fired
}

static void test_raised_limit_then_return_from_on_break() {
  resetAll();
  sit(27);
  leave(2);
  cfgSetSitLimit(40);
  sit(1);
  CHECK(app.live.mode == Mode::Studying);
  CHECK_EQ(app.live.dueSec, 0);
  CHECK_EQ(app.live.sitSec, 28);
}

static void test_left_before_alert_is_not_on_time() {
  resetAll();
  sit(20);
  leave(1);
  cfgSetSitLimit(10);  // limit now below the sit time, but no alert ever fired
  leave(4);
  CHECK_EQ(app.day.breaksTaken, 1);
  CHECK_EQ(app.day.breaksOnTime, 0);
}

// ---------------------------------------------------------------- focus score + rollover (phase 9)

static int8_t focusFor(uint32_t studyMin, uint16_t d, uint16_t s) {
  Daily day;
  day.studySec = studyMin * MIN_S;
  day.distractions = d;
  day.breaksSkipped = s;
  return computeFocus(day);
}

static void test_focus_score_guide_rows() {
  CHECK_EQ(focusFor(8, 1, 0), -1);    // under 10 min: no score yet
  CHECK_EQ(focusFor(20, 4, 0), 68);   // 30-min floor: 8.0 / h
  CHECK_EQ(focusFor(50, 2, 0), 90);   // 2.4 / h
  CHECK_EQ(focusFor(120, 6, 1), 73);  // 3.0 / h, one skip
  CHECK_EQ(focusFor(180, 30, 3), 15); // 10.0 / h, three skips
}

static void test_focus_edges_and_caps() {
  CHECK_EQ(focusFor(0, 0, 0), -1);
  CHECK_EQ(focusFor(9, 0, 0), -1);
  CHECK_EQ(focusFor(10, 0, 0), 100);      // exactly 10 min shows
  CHECK_EQ(focusFor(60, 0, 100), 55);     // skips cap at 45
  CHECK_EQ(focusFor(60, 60000, 0), 60);   // distraction rate caps at 40
  CHECK_EQ(focusFor(60, 60000, 100), 15); // both caps: never below 15 without the MPU
  Daily d;
  d.studySec = 60 * MIN_S;
  d.idleSec = 60 * MIN_S;  // all idle
  CHECK_EQ(computeFocus(d), USE_MPU ? 85 : 100);  // idle only counts with the MPU fitted
}

static void test_focus_updates_every_tick() {
  resetAll();
  sit(9);
  CHECK_EQ(app.live.focus, -1);
  sit(1);
  CHECK_EQ(app.live.focus, 100);
  CHECK_STR(screenNow().focus, "Focus 100");
}

static void test_rollover_first_sync_adopts_the_date() {
  resetAll();
  app.day.studySec = 600;  // restored from flash, date unknown
  CHECK(!dayRollover(20261031));
  CHECK_EQ(app.day.ymd, 20261031);
  CHECK_EQ(app.day.studySec, 600);
  CHECK_EQ(countSignal(Signal::DayRolled), 0);
}

static void test_rollover_same_day_keeps_counters() {
  resetAll();
  app.day.ymd = 20261031;
  app.day.distractions = 3;
  CHECK(!dayRollover(20261031));
  CHECK_EQ(app.day.distractions, 3);
  CHECK_EQ(countSignal(Signal::DayRolled), 0);
}

static void test_rollover_new_day_signals_then_resets() {
  resetAll();
  app.day.ymd = 20261031;
  sit(60);
  app.day.distractions = 2;
  CHECK(dayRollover(20261101));
  CHECK_EQ(countSignal(Signal::DayRolled), 1);
  CHECK_EQ(dayAtSignal.studySec, 60);  // the final score was still readable
  CHECK_EQ(dayAtSignal.distractions, 2);
  CHECK_EQ(app.day.studySec, 0);
  CHECK_EQ(app.day.distractions, 0);
  CHECK_EQ(app.day.ymd, 20261101);
  CHECK_EQ(app.day.version, Daily{}.version);
  CHECK(!dayRollover(20261101));  // once only
  CHECK_EQ(countSignal(Signal::DayRolled), 1);
}

static void test_rollover_without_time_does_nothing() {
  resetAll();
  app.day.ymd = 20261031;
  app.day.studySec = 50;
  CHECK(!dayRollover(0));  // NTP not synced yet
  CHECK_EQ(app.day.ymd, 20261031);
  CHECK_EQ(app.day.studySec, 50);
}

static void test_rollover_mid_session_keeps_the_sitting() {
  resetAll();
  app.day.ymd = 20261031;
  sit(12);
  dayRollover(20261101);
  CHECK(app.ses.active);
  CHECK_EQ(app.live.sitSec, 12);
  CHECK(app.live.mode == Mode::Studying);
  sit(1);
  CHECK_EQ(app.day.studySec, 1);  // the new day counts from midnight
}

// ---------------------------------------------------------------- eye-rest (phase 10)

static void test_eye_rest_at_20_with_a_25_limit() {
  resetAll();
  sit(19);
  CHECK_EQ(countSignal(Signal::EyeRest), 0);
  sit(1);
  CHECK_EQ(countSignal(Signal::EyeRest), 1);
  CHECK_EQ(app.live.eyeRestLeft, EYE_REST_SECONDS);
  CHECK(screenNow().eyeRest);
  sit(EYE_REST_SECONDS);
  CHECK_EQ(app.live.eyeRestLeft, 0);
  CHECK(!screenNow().eyeRest);
  sit(40);  // into BREAK TIME and past it: no second reminder
  CHECK_EQ(countSignal(Signal::EyeRest), 1);
}

static void test_eye_rest_never_with_a_20_limit() {
  resetAll();
  cfgSetSitLimit(20);  // the break itself arrives at 20
  sit(60);
  CHECK_EQ(countSignal(Signal::EyeRest), 0);
}

static void test_eye_rest_skipped_when_break_is_close() {
  resetAll();
  cfgSetSitLimit(30);  // 20 fires; the next one would land after the break
  sit(29);
  CHECK_EQ(countSignal(Signal::EyeRest), 1);
  resetAll();
  app.cfg.sitLimitMin = 22;  // test-only: 20 + 3 >= 22, too close (the slider stops at 10, step 5)
  sit(21);
  CHECK_EQ(countSignal(Signal::EyeRest), 0);
}

static void test_eye_rest_twice_with_a_50_limit() {
  resetAll();
  cfgSetSitLimit(50);
  sit(49);
  CHECK_EQ(countSignal(Signal::EyeRest), 2);
}

static void test_eye_rest_switch_off() {
  resetAll();
  CHECK(cfgSetEyeRest(0));
  CHECK(!app.cfg.eyeRestOn);
  CHECK(!cfgSetEyeRest(0));
  sit(24);
  CHECK_EQ(countSignal(Signal::EyeRest), 0);
  CHECK(cfgSetEyeRest(7));
  CHECK(app.cfg.eyeRestOn);
  CHECK_EQ(storagePersist() & SAVED_SETTINGS, SAVED_SETTINGS);
}

static void test_eye_rest_cleared_by_standing_up() {
  resetAll();
  sit(21);
  CHECK(app.live.eyeRestLeft > 0);
  leave(1);
  CHECK_EQ(app.live.eyeRestLeft, 0);
  CHECK(!screenNow().eyeRest);
}

static void test_eye_timer_restarts_after_a_break() {
  resetAll();
  sit(15);
  leave(5);  // break completes
  sit(19);
  CHECK_EQ(countSignal(Signal::EyeRest), 0);
  sit(1);
  CHECK_EQ(countSignal(Signal::EyeRest), 1);
}

static void test_led_sweep() {
  CHECK_EQ(sweepMask(0), 0x01);
  CHECK_EQ(sweepMask(LED_SWEEP_STEP_MS - 1), 0x01);
  CHECK_EQ(sweepMask(LED_SWEEP_STEP_MS), 0x02);
  CHECK_EQ(sweepMask(4 * LED_SWEEP_STEP_MS), 0x10);
  CHECK_EQ(sweepMask(5 * LED_SWEEP_STEP_MS), 0);
  CHECK_EQ(sweepMask(100000), 0);

  resetAll();
  app.live.mode = Mode::Studying;
  app.live.sitSec = 20;  // all five LEDs normally
  fake::now = 1000;
  ledSweep();
  ledsUpdate();
  CHECK_EQ(fake::pins[PIN_LEDS[0]], HIGH);
  CHECK_EQ(fake::pins[PIN_LEDS[1]], LOW);
  fake::now += 2 * LED_SWEEP_STEP_MS;
  ledsUpdate();
  CHECK_EQ(fake::pins[PIN_LEDS[2]], HIGH);
  CHECK_EQ(fake::pins[PIN_LEDS[0]], LOW);
  fake::now += 3 * LED_SWEEP_STEP_MS;  // sweep over: back to the progress bar
  ledsUpdate();
  CHECK_EQ(fake::pins[PIN_LEDS[0]], HIGH);
  CHECK_EQ(fake::pins[PIN_LEDS[4]], HIGH);
}

// ---------------------------------------------------------------- session summary (phase 11)

static Session sessionOf(uint32_t studyMin, uint16_t breaks, uint16_t dist) {
  Session s;
  s.studySec = studyMin * MIN_S;
  s.breaks = breaks;
  s.distractions = dist;
  return s;
}

static void test_summary_text() {
  char out[SUMMARY_LEN];
  CHECK(formatSummary(out, sizeof out, sessionOf(52, 3, 4), 81));
  CHECK_STR(out, "You studied 52 min, took 2 breaks, had 4 distractions. Focus today: 81.");
  CHECK(formatSummary(out, sizeof out, sessionOf(30, 2, 1), -1));  // singular, no score yet
  CHECK_STR(out, "You studied 30 min, took 1 break, had 1 distraction. Focus today: --.");
  CHECK(formatSummary(out, sizeof out, sessionOf(5, 0, 0), 100));  // no underflow below 0 breaks
  CHECK_STR(out, "You studied 5 min, took 0 breaks, had 0 distractions. Focus today: 100.");
}

static void test_summary_skips_short_sessions() {
  char out[SUMMARY_LEN] = "stale";
  CHECK(!formatSummary(out, sizeof out, sessionOf(MIN_SUMMARY_MIN - 1, 1, 9), 50));
  CHECK_STR(out, "");
  Session s;
  s.studySec = MIN_SUMMARY_MIN * MIN_S - 1;  // a second short
  CHECK(!formatSummary(out, sizeof out, s, 50));
}

static void test_summary_worst_case_fits() {
  char out[SUMMARY_LEN];
  CHECK(formatSummary(out, sizeof out, sessionOf(4000000, 65535, 65535), 100));
  CHECK(strlen(out) < SUMMARY_LEN - 1);  // not truncated
  char tiny[8];
  CHECK(formatSummary(tiny, sizeof tiny, sessionOf(52, 3, 4), 81));  // truncates, never overflows
  CHECK_EQ(strlen(tiny), 7);
}

static void test_summary_gate_flow() {
  resetAll();
  sit(30);    // past the 25 limit: grace runs out, one skip (focus 85)
  leave(5);   // one real break
  sit(5);
  leave(10);  // final departure ends the session
  CHECK_EQ(countSignal(Signal::SessionEnded), 1);
  char out[SUMMARY_LEN];
  CHECK(formatSummary(out, sizeof out, app.ses, app.live.focus));
  CHECK_STR(out, "You studied 35 min, took 1 break, had 0 distractions. Focus today: 85.");
}

static void test_summary_counts_this_sessions_distractions() {
  resetAll();
  soundSetBaseline(50);
  app.cfg.noiseMargin = 100;  // a clap is anything above 150
  sit(10);
  CHECK(checkDistraction(400, 10000));
  sit(5);
  CHECK(checkDistraction(400, 20000));
  leave(5);                              // a real break, before the alert
  CHECK(!checkDistraction(400, 30000));  // away: not a distraction
  sit(10);
  CHECK(checkDistraction(400, 40000));
  leave(10);  // session ends
  CHECK_EQ(countSignal(Signal::SessionEnded), 1);
  char out[SUMMARY_LEN];
  CHECK(formatSummary(out, sizeof out, app.ses, app.live.focus));
  // T = 25 min (30-min floor), 3 distractions = 6 / h: 100 - 24 = 76
  CHECK_STR(out, "You studied 25 min, took 1 break, had 3 distractions. Focus today: 76.");

  sit(10);  // next session starts from zero; the day keeps its total
  leave(10);
  CHECK_EQ(countSignal(Signal::SessionEnded), 2);
  CHECK(formatSummary(out, sizeof out, app.ses, app.live.focus));
  // T = 35 min, 3 distractions = 5.1 / h: 100 - 21 = 79
  CHECK_STR(out, "You studied 10 min, took 0 breaks, had 0 distractions. Focus today: 79.");
  CHECK_EQ(app.day.distractions, 3);
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
    test_setters_clamp_before_narrowing, test_setters_report_real_changes_only,
    test_first_boot_keeps_defaults, test_settings_survive_a_reboot,
    test_counters_survive_a_reboot, test_wrong_size_blob_is_rejected,
    test_daily_from_other_build_is_rejected, test_corrupt_settings_are_clamped_on_load,
    test_persist_writes_only_what_changed, test_reconnect_resync_costs_no_writes,
    test_slider_drag_is_one_write, test_failed_write_is_retried,
    test_reset_today_is_saved, test_same_daily_ignores_padding,
    test_slider_change_shows_on_screen_at_once, test_shorter_break_completes_next_tick,
    test_raised_limit_during_break_due_resumes_studying,
    test_raised_limit_keeps_a_counted_skip,
    test_raised_limit_during_on_break_keeps_on_time,
    test_raised_limit_then_return_from_on_break, test_left_before_alert_is_not_on_time,
    test_focus_score_guide_rows,
    test_focus_edges_and_caps,
    test_focus_updates_every_tick,
    test_rollover_first_sync_adopts_the_date,
    test_rollover_same_day_keeps_counters,
    test_rollover_new_day_signals_then_resets,
    test_rollover_without_time_does_nothing,
    test_rollover_mid_session_keeps_the_sitting,
    test_eye_rest_at_20_with_a_25_limit,
    test_eye_rest_never_with_a_20_limit,
    test_eye_rest_skipped_when_break_is_close,
    test_eye_rest_twice_with_a_50_limit,
    test_eye_rest_switch_off,
    test_eye_rest_cleared_by_standing_up,
    test_eye_timer_restarts_after_a_break,
    test_led_sweep,
    test_summary_text,
    test_summary_skips_short_sessions,
    test_summary_worst_case_fits,
    test_summary_gate_flow,
    test_summary_counts_this_sessions_distractions,
    test_no_distraction_in_break_time, test_distraction_counts_again_after_the_break,
  };
  for (auto t : tests) t();
  printf("%d checks, %d failures, %zu tests\n", checks, failures, sizeof tests / sizeof *tests);
  return failures ? 1 : 0;
}
