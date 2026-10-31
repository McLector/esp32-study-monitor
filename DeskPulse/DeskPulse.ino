// DeskPulse 2.0: 1.0's behaviour on the new pin map and module layout, plus the
// OLED status screen, configurable Pomodoro + focus score (settings and today's
// counters kept in NVS), 20-20-20 eye-rest and a session summary.
// A once-a-second Serial line mirrors the screen for logging.
#include "secrets.h"  // template defines must exist before the Blynk header
#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

#include "state.h"
#include "session.h"
#include "presence.h"
#include "sound.h"
#include "buzzer.h"
#include "env.h"
#include "screen.h"
#include "display.h"
#include "storage.h"
#include "summary.h"

AppState app;
BlynkTimer timer;

// cloud.ino
void pushAll();
void taskCloud();
void cloudOnSignal(Signal s);

constexpr long TZ_OFFSET_S = 8 * 3600;  // Manila, no daylight saving

static const char* modeName(Mode m) {
  switch (m) {
    case Mode::Studying: return "STUDYING";
    case Mode::BreakDue: return "BREAK TIME";
    case Mode::OnBreak: return "ON BREAK";
    default: return app.live.objectSuspected ? "OBJECT?" : "AWAY";
  }
}

void onSignal(Signal s) {
  switch (s) {
    case Signal::ModeChanged: Serial.printf(">> mode: %s\n", modeName(app.live.mode)); break;
    case Signal::BreakDue: Serial.println(">> break due"); break;
    case Signal::Distraction: Serial.printf(">> distraction #%u\n", app.day.distractions); break;
    case Signal::SessionEnded: Serial.println(">> session ended"); break;
    case Signal::EyeRest:
      Serial.println(">> eye rest");
      if (!app.cfg.muted) buzzerPlay(Pattern::Chirp3);  // 3 light chirps, unlike the break alert
      ledSweep();
      break;
    case Signal::DayRolled: Serial.printf(">> new day: final focus %d\n", computeFocus(app.day)); break;
    default: break;
  }
  cloudOnSignal(s);
}

static void taskOutputs() {  // every 50 ms
  alertUpdate();
  ledsUpdate();
}

static void taskDisplay() {  // every 500 ms
  displayDraw(buildScreen(WiFi.status() == WL_CONNECTED, Blynk.connected()));
}

static void taskStatus() {  // every 1 s: Serial log line
  const Live& L = app.live;
  Serial.printf("[%-10s] IR=%d PIR=%d sit=%lu/%lu away=%lu/%lu D=%u skip=%u brk=%u F=%d T=%.1f H=%.0f peak=%u wifi=%d blynk=%d\n",
                modeName(L.mode), digitalRead(PIN_IR), digitalRead(PIN_PIR),
                (unsigned long)L.sitSec, (unsigned long)sitLimitSec(), (unsigned long)L.awaySec, (unsigned long)breakSec(),
                app.day.distractions, app.day.breaksSkipped, app.day.breaksTaken, L.focus,
                L.tempC, L.humidity, L.noisePeak,
                WiFi.status() == WL_CONNECTED, Blynk.connected());
}

static void taskPersist() {  // every 60 s: flash writes only for what changed
  const uint8_t saved = storagePersist();
  if (saved & SAVED_SETTINGS)
    Serial.printf(">> saved settings: sit %u / break %u min\n", app.cfg.sitLimitMin, app.cfg.breakMin);
  if (saved & SAVED_DAILY)
    Serial.printf(">> saved counters: study %lus D=%u skip=%u brk=%u\n", (unsigned long)app.day.studySec,
                  app.day.distractions, app.day.breaksSkipped, app.day.breaksTaken);
}

static void taskRollover() {  // every 30 s: midnight reset from NTP time
  struct tm now;
  if (!getLocalTime(&now, 0)) return;  // 0 ms wait: the default blocks up to 5 s
  const uint32_t ymd = (now.tm_year + 1900) * 10000 + (now.tm_mon + 1) * 100 + now.tm_mday;
  static bool announced = false;
  if (!announced) {
    Serial.printf("NTP time %04d-%02d-%02d %02d:%02d:%02d\n", now.tm_year + 1900, now.tm_mon + 1,
                  now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
    announced = true;
  }
  dayRollover(ymd);
}

// TEST_MODE only. Type  t=120 d=6 s=1  in the Serial Monitor: sets today's study
// minutes, distractions and skipped breaks, then prints the focus score.
static void serialTestHook() {
  if (!TEST_MODE || !Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  int t, d, s;
  if (sscanf(line.c_str(), "t=%d d=%d s=%d", &t, &d, &s) == 3 && t >= 0 && d >= 0 && s >= 0) {
    app.day.studySec = t * MIN_S;
    app.day.distractions = d;
    app.day.breaksSkipped = s;
    Serial.printf("focus=%d\n", computeFocus(app.day));
  }
}

static void taskConnectivity() {  // every 10 s: report changes only
  static int lastWifi = -1, lastCloud = -1;
  const int wifi = WiFi.status() == WL_CONNECTED, cloud = Blynk.connected();
  if (wifi != lastWifi) Serial.printf("Wi-Fi %s (status %d)\n", wifi ? "up" : "down", WiFi.status());
  if (cloud != lastCloud) Serial.printf("Blynk %s\n", cloud ? "connected" : "not connected");
  lastWifi = wifi;
  lastCloud = cloud;
}

static void registerTimers() {
  timer.setInterval(50L, taskSound);
  timer.setInterval(50L, taskOutputs);
  timer.setInterval(200L, taskPresence);
  timer.setInterval(250L, taskTick);
  timer.setInterval(500L, taskDisplay);
  timer.setInterval(1000L, taskStatus);
  timer.setInterval(2500L, taskEnvironment);
  timer.setInterval(10000L, taskConnectivity);
  timer.setInterval(30000L, taskCloud);
  timer.setInterval(30000L, taskRollover);
  timer.setInterval(60000L, taskPersist);
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);  // the TEST_MODE serial hook must never stall loop() for long
  const Restored nvs = storageLoad();  // before anything reads app.cfg
  Serial.printf("NVS: settings %s (sit %u / break %u min), counters %s (study %lus D=%u skip=%u brk=%u)\n",
                nvs.settings ? "loaded" : "defaults", app.cfg.sitLimitMin, app.cfg.breakMin,
                nvs.daily ? "restored" : "fresh", (unsigned long)app.day.studySec,
                app.day.distractions, app.day.breaksSkipped, app.day.breaksTaken);
  buzzerInit();
  presenceInit();
  soundInit();
  envInit();
  if (!displayInit())  // a dead or missing screen must not stop the device
    Serial.printf("OLED not found at 0x%02X: running without the screen\n", OLED_ADDR);
  displaySplash("Calibrating sound...");

  Serial.println("Calibrating sound: stay quiet for 2 s...");
  calibrateSound();
  Serial.printf("Noise baseline %u, margin %u\n", soundBaseline(), app.cfg.noiseMargin);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Wi-Fi connecting to \"%s\" (status %d)\n", WIFI_SSID, WiFi.status());
  // Syncs once Wi-Fi is up. Literal IPs (Google Public NTP, time1-3.google.com), not
  // names: core 3.3.11's hostByName() (used by Blynk) clears the DNS cache without the
  // lwIP lock, which fires a pending SNTP lookup's callback and asserts in udp_new.
  configTime(TZ_OFFSET_S, 0, "216.239.35.0", "216.239.35.4", "216.239.35.8");
  Blynk.config(BLYNK_AUTH_TOKEN);  // non-blocking; do not use Blynk.begin()

  sessionInit();  // after calibration, so the tick clock doesn't catch up 2 s
  registerTimers();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) Blynk.run();
  timer.run();
  buzzerUpdate();
  soundSample();  // non-blocking mic sampling
  serialTestHook();
}
