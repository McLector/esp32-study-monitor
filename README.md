# ESP32 Study Monitor: DeskPulse 2.0

A desk-mounted productivity and ergonomics monitor. It tracks how long you've
actually been sitting and working, detects distractions, enforces stretch
breaks, and reports everything to a live IoT dashboard. It's a coursework
hardware project ("Smart Environment: Classroom/Office Productivity &
Ergonomics Monitor"), CS Elect 3 finals.

**DeskPulse 2.0** is a ground-up rebuild of the original 1.0 monitor (kept in
[`firmware/`](firmware/) and tagged [`v1.0`](https://github.com/McLector/esp32-study-monitor/tree/v1.0)). It adds four
enhancements on top of 1.0's behaviour:

| # | Enhancement | Status |
|---|---|---|
| 1 | OLED status screen (0.96" SSD1306) | next: phase 7 |
| 2 | Configurable Pomodoro + daily focus score | planned |
| 3 | 20-20-20 eye-rest reminders | planned |
| 4 | Session summary notification | planned |

**Progress:** phases 1 and 3–6 of 12 have passed on the real board. 1.0's
behaviour is ported to the new modular firmware and passes all five
regression tests: presence, break alert, break taken, distraction count, and
mute. Phase 2's Wi-Fi works (the board stays connected to Blynk), but its NTP
clock check hasn't been run yet. It's needed before the daily-rollover
features. See [`docs/BUILD_LOG.md`](docs/BUILD_LOG.md) for how each gate went.

**Hardware:** ESP32 (WROOM-32) · SSD1306 OLED · IR obstacle sensor · PIR
motion sensor · LM393 sound sensor · DHT11 · 5-LED bar · active buzzer
**Software:** Arduino C++, non-blocking firmware, Blynk IoT (Free plan), host
unit tests with g++

## Repository layout

| Path | What it is |
|---|---|
| [`DeskPulse/`](DeskPulse/) | 2.0 firmware (Arduino sketch folder) |
| [`DeskPulse_tests/`](DeskPulse_tests/) | Host unit tests for the logic modules, run on a PC with g++ |
| [`docs/rebuild-guide.html`](docs/rebuild-guide.html) | The 12-phase rebuild plan, with a gate test per phase. It's the spec |
| [`docs/breadboard-wiring.html`](docs/breadboard-wiring.html) | Interactive breadboard layout and wiring checklist, per phase |
| [`docs/BUILD_LOG.md`](docs/BUILD_LOG.md) | What actually happened during the build, gate by gate |
| [`firmware/`](firmware/) | The original 1.0 firmware, `StudyMonitor.ino` |

The HTML docs are single files. Download them and open them in a browser
(GitHub shows them as source).

## Pin map (2.0)

The OLED takes the ESP32's default I2C pins (21/22), so the DHT11 and IR
sensor moved off them compared to 1.0.

| Function | GPIO | Power | Notes |
|---|---|---|---|
| I2C SDA / SCL (OLED) | 21 / 22 | 3V3 | OLED at `0x3C` |
| LED 1–5, blue → red | 25, 26, 27, 32, 33 | — | 220 Ω each; fills over the sit limit |
| Buzzer signal | 19 | 3V3 | Active buzzer module, HIGH = on |
| IR obstacle OUT | 18 | **3V3** | **Direct, no divider** (see below) |
| PIR OUT | 23 | 5V (VIN / V5) | Output is already 3.3 V logic |
| DHT11 DATA | 16 | 3V3 | Module has a built-in pull-up |
| LM393 AO | 34 | 3V3 | ADC1, required while Wi-Fi is on. DO unused |

GPIO 6–11 (flash) and the strapping pins (0, 2, 5, 12, 15) are avoided.

### Why the IR module runs on 3.3 V with no divider

The plan put the IR module on 5 V with a 1 kΩ + 2 kΩ divider in front of
GPIO 18, because the ESP32 isn't 5 V tolerant. On the bench, GPIO 18 read 0
all the time, and the module's obstacle LED glowed faintly with nothing in
front of it.

The cause is that this module only holds its output HIGH weakly, through about
a 10 kΩ pull-up. The 3 kΩ divider dragged the idle output down to about 2.5 V,
which left about 1.7 V at the tap, below the ESP32's ~2.5 V "HIGH" threshold.
Pulling one resistor out made the glow disappear, which confirmed it.

Powering the module from 3.3 V means its output can never exceed 3.3 V, so it
connects straight to GPIO 18. The trade-off is range: the IR emitter is dimmer
at 3.3 V (1.0 ran it at 5 V for that reason), so detection drops to about
10 cm. That still meets the gate, and the sensor is mounted 8–10 cm from the
user. To keep 5 V, you'd need a high-value divider (about 47 kΩ over
100 kΩ) that barely loads the output.

## Firmware architecture

Fully non-blocking. `loop()` runs `Blynk.run()`, a `BlynkTimer`, the buzzer
sequencer and the mic sampler. There's no `delay()` in the main path.

| Module | Job |
|---|---|
| `DeskPulse.ino` | setup/loop, timer tasks, routes `Signal`s to Serial and the cloud |
| `cloud.ino` | The only Blynk code: datastream pushes (change-only), `break_time` event, V5/V6/V9 handlers |
| `session.*` | State machine: Away → Studying → Break due → On break, with catch-up ticks |
| `presence.*` | IR presence with a 5-read debounce, plus a PIR "object guard" (IR without motion = a bag, not a person) |
| `sound.*` | 2 s ambient baseline at boot, peak-to-peak detection, 3 s cooldown, ignores the buzzer's own beeps |
| `buzzer.*` | Non-blocking beep patterns and the LED bar |
| `env.*` | DHT11; keeps the last good reading on a failed read |
| `config.h`, `state.h` | Pins, timings, feature flags, and the shared app state |

The logic modules don't know Blynk exists. They raise signals (`ModeChanged`,
`BreakDue`, `Distraction`, …) and the `.ino` decides what each one means. That
split is what lets the logic be unit-tested on a PC.

`TEST_MODE` in `config.h` makes one app "minute" last one second, so a
25-minute sit limit takes 25 s on the bench. Set it to `false` for real use.

## Running the tests

The logic modules compile for the PC against small fakes of `Arduino.h` and
`DHT.h` in `DeskPulse_tests/shim/`:

```powershell
powershell -File DeskPulse_tests/run_tests.ps1
```

This needs g++ on the PATH. Current result: 41 tests, 390 checks, 0 failures.

## Setup

1. Install the ESP32 board core (3.x) and these Arduino libraries: **Blynk**,
   **Adafruit SSD1306**, **Adafruit GFX**, **DHT sensor library** and
   **Adafruit Unified Sensor**.
2. Copy `DeskPulse/secrets.example.h` to `DeskPulse/secrets.h` and fill in
   your Blynk template ID, template name, auth token and Wi-Fi. Easiest:
   paste the "Firmware configuration" box from Blynk.Console's Device Info
   page. `secrets.h` is gitignored, so never commit it. The Wi-Fi must be
   2.4 GHz.
3. In Blynk.Console, the template needs these virtual pins:

   | Pin | Datastream | Pin | Datastream |
   |---|---|---|---|
   | V0 | Study minutes (int) | V5 | Mute buzzer (switch) |
   | V1 | Distractions (int) | V6 | Sensitivity / noise margin (slider, 0–2000) |
   | V2 | Temperature (double, 1 dp) | V7 | At desk (0/1) |
   | V3 | Humidity (double, 1 dp) | V8 | Sit progress (0–100 %) |
   | V4 | Noise peak (0–4095) | V9 | Reset counters (button, **Push** mode) |

   It also needs the event **`break_time`**. Later phases add V10–V15 and the
   `eye_rest` and `session_summary` events. The rebuild guide lists them all.
4. Flash `DeskPulse/` to the board (board: "ESP32 Dev Module").

---

## Version 1.0 (tag `v1.0`)

The original monitor is in [`firmware/StudyMonitor.ino`](firmware/StudyMonitor.ino).
It used the same sensors on a different pin map (IR on 22, DHT11 on 21), with
no OLED. Two parts of its write-up still matter for 2.0.

### The 1.0 IR debugging story

In 1.0, the IR module's output was permanently HIGH: it never detected
anything, even fully enclosed in a hand. Power rails, jumpers and the GPIO were
each ruled out. The breakthrough was reading the pin with `INPUT_PULLUP` and
then `INPUT_PULLDOWN` and comparing the results, which tells a floating pin
apart from one that's actually being driven:

| Pullup read | Pulldown read | Meaning |
|---|---|---|
| 1 | 0 | floating: nothing driving it |
| 1 | 1 | driven HIGH externally |
| 0 | 0 | driven LOW externally |

That pointed at the module itself. It uses an NE555 to generate a ~38 kHz
carrier for its IR emitter, and one of its two trim pots tunes that frequency.
Slightly off-tune, the receiver never sees valid modulated IR, yet the module
looks completely healthy. Sweeping both pots with a hand at about 10 cm found
the working position.

### Known limitations (still true in 2.0)

- **Detection range** is about 10 cm, so mount the sensor at the desk edge,
  aimed at the torso.
- **No identity discrimination:** anyone sitting in front of it accumulates
  study time.
- **The PIR sees the whole room:** someone walking past can reset the
  object-guard timer.
- **Sound ≠ meaning:** a slammed door and a clap look the same.
- **DHT11 accuracy** is ±2 °C / ±5 % RH, fine for "is this room
  uncomfortable".
- **The noise baseline** is set once at boot.

### Lessons learned

- Bench-test every sensor on its own before loading the real firmware.
  Debouncing and calibration can make a wiring fault look almost plausible.
- The pullup/pulldown read comparison is the quickest way to tell
  "disconnected" from "not triggering".
- A module's power LED only proves VCC and GND. A **faintly** lit signal LED
  can mean something is loading the output (the 2.0 divider problem).
- ESP32 ADC2 shares hardware with the Wi-Fi radio and returns garbage while
  Wi-Fi is on. Use ADC1 (GPIO 32–39) for analog sensors.
- Wire and power up **one group at a time** (see the build log). Wiring
  everything at once means a short could be anywhere.
