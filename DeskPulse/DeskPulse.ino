// DeskPulse 2.0 — phase 6: 1.0's behaviour on the new pin map and module layout.
// No OLED yet (phase 7): a once-a-second status line on Serial stands in for it.
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

AppState app;
BlynkTimer timer;

// cloud.ino
void pushAll();
void taskCloud();
void cloudOnSignal(Signal s);

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
    default: break;
  }
  cloudOnSignal(s);
}

static void taskOutputs() {  // every 50 ms
  alertUpdate();
  ledsUpdate();
}

static void taskStatus() {  // every 1 s: the OLED's job until phase 7
  const Live& L = app.live;
  Serial.printf("[%-10s] IR=%d PIR=%d sit=%lu/%lu away=%lu D=%u skip=%u brk=%u T=%.1f H=%.0f peak=%u wifi=%d blynk=%d\n",
                modeName(L.mode), digitalRead(PIN_IR), digitalRead(PIN_PIR),
                (unsigned long)L.sitSec, (unsigned long)sitLimitSec(), (unsigned long)L.awaySec,
                app.day.distractions, app.day.breaksSkipped, app.day.breaksTaken,
                L.tempC, L.humidity, L.noisePeak,
                WiFi.status() == WL_CONNECTED, Blynk.connected());
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
  timer.setInterval(1000L, taskStatus);
  timer.setInterval(2500L, taskEnvironment);
  timer.setInterval(10000L, taskConnectivity);
  timer.setInterval(30000L, taskCloud);
}

void setup() {
  Serial.begin(115200);
  buzzerInit();
  presenceInit();
  soundInit();
  envInit();

  Serial.println("Calibrating sound: stay quiet for 2 s...");
  calibrateSound();
  Serial.printf("Noise baseline %u, margin %u\n", soundBaseline(), app.cfg.noiseMargin);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Wi-Fi connecting to \"%s\" (status %d)\n", WIFI_SSID, WiFi.status());
  Blynk.config(BLYNK_AUTH_TOKEN);  // non-blocking; do not use Blynk.begin()

  sessionInit();  // after calibration, so the tick clock doesn't catch up 2 s
  registerTimers();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) Blynk.run();
  timer.run();
  buzzerUpdate();
  soundSample();  // non-blocking mic sampling
}
