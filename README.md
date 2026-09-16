# ESP32 Study Monitor

A desk-mounted productivity and ergonomics monitor. It tracks how long you've
actually been sitting and working, detects distractions, enforces stretch
breaks, and reports everything to a live IoT dashboard — built as a coursework
hardware project ("Smart Environment — Classroom/Office Productivity &
Ergonomics Monitor").

**Hardware:** ESP32 DevKit V1 (WROOM-32) · IR obstacle sensor · PIR motion
sensor · LM393 sound sensor · DHT11 · 5-LED bar · active buzzer
**Software:** Arduino C++, non-blocking firmware, Blynk IoT 2.0 dashboard

## What it does

1. **Presence detection** — an IR obstacle sensor detects whether you're
   physically sitting at the desk. Unlike a PIR sensor, which only sees
   motion and "forgets" you the moment you sit still, this sees a static
   body.
2. **Ergonomics enforcement** — a 5-segment LED bar fills green→red over 30
   minutes of continuous sitting. When it's full, all LEDs blink and the
   buzzer sounds a repeating double-beep until you take a break.
3. **Distraction tracking** — an LM393 sound sensor detects sudden loud
   noises (claps, door slams, talking) and logs them as distraction events,
   with a cooldown so one clap doesn't register as several.
4. **Environment monitoring** — a DHT11 tracks room temperature and humidity
   to flag uncomfortable study conditions.

Everything reports to a **Blynk IoT dashboard** with live gauges, a noise
chart, and remote controls (mute switch, sensitivity slider).

## Wiring

The ESP32 isn't 5V tolerant, but two of the sensors are. Two rails are used —
`3V3` powers most modules, `VIN` (5V USB passthrough) powers the PIR and the
IR obstacle module (its NE555 design needs 5V to drive the IR emitter). The IR
module's output is stepped down to 3.3V with a resistor voltage divider before
it reaches the ESP32.

| Function | GPIO | Power | Notes |
|---|---|---|---|
| LED Red | 32 | — | through 220Ω |
| LED Orange | 33 | — | through 220Ω |
| LED Yellow | 25 | — | through 220Ω |
| LED Green | 26 | — | through 220Ω |
| LED Blue | 27 | — | through 220Ω |
| LM393 AO | 34 | 3V3 | ADC1 — required for WiFi coexistence |
| LM393 DO | 35 | 3V3 | input-only, no internal pull-up |
| IR Obstacle OUT | 22 | 5V (VIN) | through a 3×220Ω voltage divider |
| PIR OUT | 23 | 5V (VIN) | on-board regulator, output is 3.3V-safe |
| DHT11 DATA | 21 | 3V3 | module has a built-in pull-up |
| Buzzer signal | 19 | 3V3 | module has its own driver transistor |

GPIO 6–11 (SPI flash) and the strapping pins (0, 2, 4, 5, 12, 15) were
deliberately avoided — using them, or having a sensor hold one at the wrong
level at boot, causes boot failures that look like random hangs.

## The interesting bug: IR sensor debugging

The IR obstacle module's output pin was permanently HIGH — it never detected
anything, even completely enclosed in a hand. Power rails, jumpers, and the
GPIO itself were all ruled out individually (the same rail was already
driving working LEDs and a buzzer).

The breakthrough was reading the pin with `INPUT_PULLUP` and then
`INPUT_PULLDOWN` and comparing the two results — that distinguishes a floating
pin from one actually being driven:

| Pullup read | Pulldown read | Meaning |
|---|---|---|
| 1 | 0 | floating — nothing driving it |
| 1 | 1 | driven HIGH externally |
| 0 | 0 | driven LOW externally |

That pointed at the sensor itself, not the wiring. The module uses an NE555
chip (confirmed by reading the physical markings on the chip, not a stock
photo — the board had been misidentified earlier from a compressed reference
image) to generate a ~38 kHz carrier for its IR emitter. One of its two
trim pots tunes that carrier frequency. If it's off-tune even slightly, the
receiver never sees valid modulated IR — and the module looks completely
healthy the whole time: power LED on, emitter lit, all connections good, just
never detecting anything.

Sweeping both pots while holding a hand ~10cm from the sensor found the
position where detection started working. The module also turned out to need
5V, not the 3.3V it was originally wired for — the emitter was too dim at
3.3V.

## Firmware architecture

Fully non-blocking. `loop()` only runs `Blynk.run()`, a buzzer sequencer, and
a `BlynkTimer` — no `delay()` anywhere in the main path. All real work is
timed tasks:

| Interval | Task | Purpose |
|---|---|---|
| 50 ms | `taskSound` | peak-to-peak noise sampling |
| 200 ms | `taskPresence` | debounced IR + PIR presence evaluation |
| 1000 ms | `taskTick` | core state machine — sit timer, study counter, alerts |
| 2000 ms | `taskPush` | write all values to Blynk |
| 2500 ms | `taskEnvironment` | DHT11 temperature & humidity read |
| 30000 ms | `taskDailyRollover` | NTP check — reset daily counters at local midnight |

Presence is a debounced two-state machine (`AT_DESK` / `AWAY`), needing 5
consecutive readings before it flips. An **object guard** cross-references the
IR sensor against the PIR: if the IR sees something but the PIR hasn't seen
motion in 5 minutes, the firmware assumes it's an object (a backpack, a book)
rather than a person, and pauses the study timer.

## Known limitations

Documented honestly rather than hidden:

- **Detection range** is 10–15cm — the sensor has to be mounted at the desk
  edge, aimed at the user's lap or torso.
- **No identity discrimination** — anyone sitting in front of it accumulates
  study time.
- **PIR sees the whole room**, not just the chair — someone walking past can
  reset the object-guard timer even after the actual user has left.
- **No persistence across reboots** — all counters reset on power cycle.
- **Sound ≠ meaning** — the sensor measures amplitude, not content. A slammed
  door and a clap look the same.
- **DHT11 accuracy** is ±2°C / ±5% RH — fine for "is this room uncomfortable,"
  not for anything requiring precision.
- **Single-calibration noise baseline** — set once at boot; a big shift in
  ambient noise during a session needs a reset to recalibrate.

## Lessons learned

- Bench-test every sensor individually before loading the real firmware — the
  full firmware's debouncing and calibration logic can make a wiring fault
  look almost plausible, hiding the actual problem.
- The pullup/pulldown read comparison is the single most useful diagnostic
  for telling "disconnected" apart from "not triggering."
- A module's power LED only proves VCC and GND are connected — it says
  nothing about whether the signal pin actually works.
- A GPIO's internal pull-up can back-feed a module and light its LEDs, making
  it look alive when the signal path isn't actually working.
- ESP32's ADC2 shares hardware with the WiFi radio and fails silently under
  WiFi — analog reads on ADC2 pins return garbage with no error while WiFi is
  active. Always use ADC1 (GPIO 32–39) for analog sensors on WiFi-connected
  boards.

## Setup

1. Install the Arduino libraries: `Blynk` (Volodymyr Shymanskyy), `DHT sensor
   library` (Adafruit), `Adafruit Unified Sensor` (Adafruit).
2. Copy `firmware/secrets.h.example` to `firmware/secrets.h` and fill in your
   own Blynk template ID/name, auth token, and WiFi credentials. `secrets.h`
   is gitignored — never commit it.
3. In the Blynk dashboard, create the 10 virtual pin datastreams the firmware
   expects (`V0`–`V9`: study minutes, distractions, temperature, humidity,
   noise level, mute switch, sensitivity slider, at-desk flag, sit progress,
   reset button). `V9` (reset) must be **Push mode**, not Switch.
4. Flash `firmware/StudyMonitor.ino` to an ESP32 DevKit V1.
5. `TEST_MODE` in the firmware compresses all timings (30s sit limit instead
   of 30 minutes) for fast bench testing — flip it to `false` for real use.
