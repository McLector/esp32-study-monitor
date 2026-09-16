/* =========================================================================
   Smart Environment — Desk Productivity & Ergonomics Monitor
   ESP32 DevKit V1 (WROOM-32, 30-pin) + Blynk

   Libraries required (Library Manager):
     - Blynk                    by Volodymyr Shymanskyy
     - DHT sensor library       by Adafruit
     - Adafruit Unified Sensor  by Adafruit

   Architecture: everything is non-blocking. loop() only runs Blynk.run(),
   the buzzer sequencer, and BlynkTimer. No delay() anywhere in the main path.
   ========================================================================= */

/* ---- Credentials: copy secrets.h.example -> secrets.h and fill in your
   own Blynk template/token and WiFi SSID/password. secrets.h is gitignored. ---- */
#include "secrets.h"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

char ssid[] = WIFI_SSID;   // exact spelling + case, 2.4 GHz only
char pass[] = WIFI_PASS;

/* =========================== CONFIGURATION =========================== */

// --- Pins ---
// LED array is in ESCALATING order for the progress bar: green fills first,
// red last. Physical colours: 32=red 33=orange 25=yellow 26=green 27=blue.
const uint8_t LED_PINS[5] = {26, 27, 25, 33, 32};  // green, blue, yellow, orange, red
const uint8_t PIN_BUZZER  = 19;   // drive the MODULE's signal pin, not a bare buzzer
const uint8_t PIN_DHT     = 21;   // 3-pin module: pull-up already on the PCB
const uint8_t PIN_IR      = 22;   // IR obstacle -> desk presence
const uint8_t PIN_PIR     = 23;   // PIR motion  -> "is it a human?"
const uint8_t PIN_SOUND_D = 35;   // LM393 digital out — input-only, no internal pull-up
const uint8_t PIN_SOUND_A = 34;   // LM393 analog out — MUST be ADC1 (32-39)

// --- Presence source ---
// IR obstacle module is working (tuned pots, 5V power, voltage divider on OUT).
// Set true to fall back to PIR motion-latch if the IR module fails again.
const bool PRESENCE_FROM_PIR = false;

// --- Hardware quirks ---
const bool IR_ACTIVE_LOW    = true;   // confirmed: HIGH idle, LOW on detection
const bool SOUND_D_ACTIVE_LOW = true; // LM393 DO is usually active LOW
const bool BUZZER_ACTIVE_TYPE = true; // confirmed: active buzzer (just needs DC)

// --- Timing. Flip TEST_MODE to false for the real 30-minute behaviour. ---
const bool     TEST_MODE        = true;
const uint32_t SIT_LIMIT_S      = TEST_MODE ?  30 : 30UL * 60;  // sit before alert
const uint32_t BREAK_REQUIRED_S = TEST_MODE ?   5 : 2UL  * 60;  // away = break taken
const uint32_t OBJECT_TIMEOUT_S = TEST_MODE ?  20 : 5UL  * 60;  // IR w/o PIR = object
const uint32_t ALERT_REPEAT_MS  = TEST_MODE ? 5000 : 20000;

// Motion latch for PIR presence. Any motion marks you present; you stay present
// until this long passes with NO motion. Too short and the sit timer resets
// while you sit reading; too long and an empty desk keeps counting study time.
const uint32_t PRESENCE_HOLD_S  = TEST_MODE ?   15 : 90;

// --- Debounce ---
const uint8_t  PRESENCE_SAMPLES  = 5;     // consecutive 200ms reads to flip state
const uint32_t DISTRACTION_COOLDOWN_MS = 3000;  // one clap = one event

// --- Sound ---
const uint16_t SOUND_WINDOW_MS   = 25;    // peak-to-peak sampling window
const uint32_t CALIBRATION_MS    = 2000;  // ambient baseline at boot
int            noiseMargin       = 400;   // above baseline = distraction (V6 slider)

// The LM393's DO pin is a fixed pot-tuned comparator. If the pot is off it can
// sit permanently triggered and spam events. Default OFF; the analog path is
// self-calibrating. If you enable it, it acts as CONFIRMATION (AND), not a
// second trigger (OR) — both must agree before an event is logged.
const bool USE_SOUND_DO = false;

// --- Object guard ---
// PIR sees the whole room, not just your chair. This cross-checks the IR sensor
// against the PIR — if the IR sees something but nothing has moved for a long
// time, it's probably an object, not a person.
const bool USE_PIR_GUARD = true;

// --- Daily rollover (NTP) ---
const long GMT_OFFSET_SEC = 8 * 3600;   // Philippines = UTC+8
const int  DST_OFFSET_SEC = 0;

// --- Blynk virtual pins ---
#define VP_STUDY_MIN    V0
#define VP_DISTRACTIONS V1
#define VP_TEMP         V2
#define VP_HUMIDITY     V3
#define VP_NOISE        V4
#define VP_MUTE         V5
#define VP_SENSITIVITY  V6
#define VP_AT_DESK      V7
#define VP_PROGRESS     V8
#define VP_RESET        V9

/* ============================== STATE ================================ */

DHT dht(PIN_DHT, DHT11);
BlynkTimer timer;

bool     atDesk            = false;
uint8_t  presenceStreak    = 0;
bool     lastRawPresence   = false;

uint32_t sitSeconds        = 0;    // continuous sitting, resets on real break
uint32_t studySeconds      = 0;    // cumulative for the day
uint32_t awaySeconds       = 0;
uint32_t secondsSinceMotion = 0;   // for the object-vs-human check
uint32_t lastMotionMs      = 0;    // millis() of last PIR trigger; 0 = never

bool     alertActive       = false;
uint32_t lastAlertMs       = 0;

uint32_t distractionCount  = 0;
uint32_t lastDistractionMs = 0;
int      noiseBaseline     = 0;
int      noiseLevel        = 0;

bool     muted             = false;
float    temperature       = NAN;
float    humidity          = NAN;
int      lastDayOfYear     = -1;   // for the NTP midnight rollover

// Non-blocking buzzer sequencer (double-beep pattern)
const uint16_t BEEP_PATTERN[] = {120, 90, 120, 0};  // on, off, on, terminator
uint8_t  beepStep = 0;
uint32_t beepNextMs = 0;
bool     beepRunning = false;

/* ============================ HELPERS ================================ */

void buzzerOn() {
  if (muted) return;
  if (BUZZER_ACTIVE_TYPE) digitalWrite(PIN_BUZZER, HIGH);
  else                    tone(PIN_BUZZER, 2000);   // needs ESP32 core >= 2.0.3
}

void buzzerOff() {
  if (BUZZER_ACTIVE_TYPE) digitalWrite(PIN_BUZZER, LOW);
  else                    noTone(PIN_BUZZER);
}

void startBeep() {
  if (muted) return;
  beepStep = 0;
  beepRunning = true;
  beepNextMs = millis();
}

// Called from loop() so beep timing isn't limited by timer resolution.
void serviceBuzzer() {
  if (!beepRunning) return;
  if (muted) { beepRunning = false; buzzerOff(); return; }
  if ((int32_t)(millis() - beepNextMs) < 0) return;

  if (BEEP_PATTERN[beepStep] == 0) {     // pattern finished
    buzzerOff();
    beepRunning = false;
    return;
  }
  if (beepStep % 2 == 0) buzzerOn(); else buzzerOff();
  beepNextMs = millis() + BEEP_PATTERN[beepStep];
  beepStep++;
}

void setLedBar(uint8_t litCount) {
  for (uint8_t i = 0; i < 5; i++)
    digitalWrite(LED_PINS[i], i < litCount ? HIGH : LOW);
}

void blinkAllLeds(bool on) {
  for (uint8_t i = 0; i < 5; i++) digitalWrite(LED_PINS[i], on);
}

bool readPresenceRaw() {
  if (PRESENCE_FROM_PIR) {
    // Latched: present if the PIR has seen motion within the hold window.
    if (lastMotionMs == 0) return false;          // no motion since boot
    return (millis() - lastMotionMs) < (PRESENCE_HOLD_S * 1000UL);
  }
  int v = digitalRead(PIN_IR);
  return IR_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

bool readSoundDigital() {
  int v = digitalRead(PIN_SOUND_D);
  return SOUND_D_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

// Peak-to-peak amplitude over a short window = actual loudness.
// A single analogRead() would just return one point on the waveform.
int sampleSoundAmplitude(uint16_t windowMs) {
  uint32_t start = millis();
  int lo = 4095, hi = 0;
  while (millis() - start < windowMs) {
    int s = analogRead(PIN_SOUND_A);
    if (s < lo) lo = s;
    if (s > hi) hi = s;
  }
  return hi - lo;
}

/* =========================== TIMER TASKS ============================= */

// Every 60 ms — noise detection
void taskSound() {
  noiseLevel = sampleSoundAmplitude(SOUND_WINDOW_MS);

  bool spike = (noiseLevel > noiseBaseline + noiseMargin);
  if (USE_SOUND_DO) spike = spike && readSoundDigital();   // confirmation, not OR
  bool cooled = (millis() - lastDistractionMs) > DISTRACTION_COOLDOWN_MS;

  if (spike && cooled && atDesk) {
    distractionCount++;
    lastDistractionMs = millis();
    Blynk.virtualWrite(VP_DISTRACTIONS, distractionCount);
    Serial.printf("[SOUND] Distraction #%lu  (level %d / thr %d)\n",
                  distractionCount, noiseLevel, noiseBaseline + noiseMargin);
  }
}

// Every 200 ms — presence debounce
void taskPresence() {
  // Sample the PIR first so the latch is current before presence is evaluated.
  if (digitalRead(PIN_PIR) == HIGH) lastMotionMs = millis();

  bool raw = readPresenceRaw();

  if (raw == lastRawPresence) {
    if (presenceStreak < 255) presenceStreak++;
  } else {
    presenceStreak = 1;
    lastRawPresence = raw;
  }

  if (presenceStreak >= PRESENCE_SAMPLES && atDesk != raw) {
    atDesk = raw;
    awaySeconds = 0;
    secondsSinceMotion = 0;
    Blynk.virtualWrite(VP_AT_DESK, atDesk ? 1 : 0);
    Serial.printf("[PRESENCE] %s\n", atDesk ? "AT DESK" : "AWAY");
  }

  if (digitalRead(PIN_PIR) == HIGH) secondsSinceMotion = 0;
}

// Every 1000 ms — the core state machine
void taskTick() {
  if (atDesk) {
    secondsSinceMotion++;
    awaySeconds = 0;

    // Object guard: IR sees something but nothing has moved in a long time.
    bool likelyObject = USE_PIR_GUARD && (secondsSinceMotion > OBJECT_TIMEOUT_S);

    if (!likelyObject) {
      sitSeconds++;
      studySeconds++;
    }

    if (sitSeconds >= SIT_LIMIT_S) {
      if (!alertActive) {
        alertActive = true;
        lastAlertMs = 0;                       // fire immediately
        Serial.println("[ALERT] Stand up and stretch.");
      }
    }
  } else {
    awaySeconds++;
    if (awaySeconds >= BREAK_REQUIRED_S && sitSeconds > 0) {
      sitSeconds = 0;
      alertActive = false;
      Serial.println("[BREAK] Counted. Sit timer reset.");
    }
  }

  // --- Visual output ---
  if (alertActive) {
    blinkAllLeds((millis() / 250) % 2);        // fast blink = get up
    if (millis() - lastAlertMs > ALERT_REPEAT_MS) {
      lastAlertMs = millis();
      startBeep();
    }
  } else {
    uint8_t lit = (sitSeconds * 5) / SIT_LIMIT_S;
    if (lit > 5) lit = 5;
    setLedBar(atDesk ? lit : 0);
  }
}

// Every 2500 ms — DHT11 (its own max sample rate is ~1 Hz)
void taskEnvironment() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    humidity = h;
    temperature = t;
  }
}

// Every 2000 ms — push to Blynk
void taskPush() {
  Blynk.virtualWrite(VP_STUDY_MIN, studySeconds / 60);
  Blynk.virtualWrite(VP_NOISE, noiseLevel);
  Blynk.virtualWrite(VP_AT_DESK, atDesk ? 1 : 0);

  int progress = (int)((sitSeconds * 100UL) / SIT_LIMIT_S);
  Blynk.virtualWrite(VP_PROGRESS, progress > 100 ? 100 : progress);

  if (!isnan(temperature)) Blynk.virtualWrite(VP_TEMP, temperature);
  if (!isnan(humidity))    Blynk.virtualWrite(VP_HUMIDITY, humidity);
}

// Every 30 s — reset the daily counters at local midnight.
// Falls back silently to "never reset" if NTP hasn't synced yet.
void taskDailyRollover() {
  struct tm t;
  if (!getLocalTime(&t, 100)) return;          // not synced yet, try again later

  if (lastDayOfYear < 0) {                     // first successful sync
    lastDayOfYear = t.tm_yday;
    Serial.printf("[TIME] Synced: %04d-%02d-%02d %02d:%02d\n",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    return;
  }

  if (t.tm_yday != lastDayOfYear) {
    lastDayOfYear = t.tm_yday;
    studySeconds = 0;
    distractionCount = 0;
    Blynk.virtualWrite(VP_STUDY_MIN, 0);
    Blynk.virtualWrite(VP_DISTRACTIONS, 0);
    Serial.println("[TIME] Midnight — daily counters reset.");
  }
}

/* =========================== BLYNK HANDLERS ========================== */

BLYNK_WRITE(VP_MUTE) {
  muted = param.asInt();
  if (muted) { buzzerOff(); beepRunning = false; }
  Serial.printf("[BLYNK] Buzzer %s\n", muted ? "muted" : "unmuted");
}

BLYNK_WRITE(VP_SENSITIVITY) {
  noiseMargin = param.asInt();
  Serial.printf("[BLYNK] Noise margin = %d\n", noiseMargin);
}

BLYNK_WRITE(VP_RESET) {
  if (param.asInt() == 1) {
    sitSeconds = studySeconds = distractionCount = 0;
    alertActive = false;
    Serial.println("[BLYNK] Counters reset.");
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(VP_MUTE, VP_SENSITIVITY);
}

/* ============================== SETUP ================================ */

void calibrateNoiseBaseline() {
  Serial.print("Calibrating ambient noise (stay quiet)");
  uint32_t start = millis();
  long sum = 0; int n = 0;
  while (millis() - start < CALIBRATION_MS) {
    sum += sampleSoundAmplitude(SOUND_WINDOW_MS);
    n++;
    if (n % 20 == 0) Serial.print(".");
  }
  noiseBaseline = (n > 0) ? (int)(sum / n) : 0;
  Serial.printf(" baseline = %d\n", noiseBaseline);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  for (uint8_t i = 0; i < 5; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }
  pinMode(PIN_BUZZER, OUTPUT);
  buzzerOff();

  pinMode(PIN_IR, INPUT_PULLUP);   // FC-51 is push-pull; pull-up is harmless insurance
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_SOUND_D, INPUT);     // GPIO35 is input-only — relies on the module's
                                   // on-board pull-up. See README note if DO is noisy.
  // PIN_SOUND_A (GPIO34) is input-only; no pinMode needed.

  dht.begin();

  calibrateNoiseBaseline();

  Serial.println("PIR warming up (~60s) — ignore motion readings until then.");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // WiFi is up by this point, so NTP can start.
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");

  timer.setInterval(50L,    taskSound);
  timer.setInterval(200L,   taskPresence);
  timer.setInterval(1000L,  taskTick);
  timer.setInterval(2500L,  taskEnvironment);
  timer.setInterval(2000L,  taskPush);
  timer.setInterval(30000L, taskDailyRollover);
}

void loop() {
  Blynk.run();
  timer.run();
  serviceBuzzer();
}
