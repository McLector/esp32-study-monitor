# DeskPulse 2.0: build log

What actually happened while building DeskPulse 2.0, phase by phase: what was
tried, what broke, how it was diagnosed, and the evidence each gate passed on.
The plan itself is in [`rebuild-guide.html`](rebuild-guide.html). This file is
the record of carrying it out. Add a new entry each time a gate passes or
something goes wrong.

| Phase | Gate | Result | Date |
|---|---|---|---|
| 01 Toolchain and template | Every library compiles | ✅ passed | Oct 2026 |
| 02 Bare ESP32 | Correct Manila time within 10 s | ⏳ Wi-Fi confirmed; NTP check not run yet | — |
| 03 I2C bus and OLED | Scanner finds `0x3C`; text readable | ✅ passed | 31 Oct 2026 |
| 04 Outputs | LEDs chase 1→5; buzzer clicks | ✅ passed | 31 Oct 2026 |
| 05 Inputs | IR 1 idle / 0 at 10 cm; PIR 0 at rest; clap spikes; DHT plausible | ✅ passed, after an IR wiring change | 31 Oct 2026 |
| 06 Core firmware port | 1.0's five regression tests | ✅ passed | 31 Oct 2026 |
| 07 OLED status screen | Every state shows its screen; timers tick in real time | ✅ happy path on the board; 3 checks not run on hardware | 31 Oct 2026 |
| 08–12 | | planned | |

---

## Before 31 Oct: firmware first, on the PC

The phase 6 firmware (1.0's behaviour on the new pin map, split into modules)
was written and tested **before** it touched hardware. The logic modules
(session state machine, presence debounce and object guard, sound
calibration and cooldown, buzzer sequencer, DHT handling) compile on a PC
against small fake `Arduino.h` and `DHT.h` headers.

- Host tests: **41 tests, 390 checks, 0 failures**, covering edge cases such
  as a stalled timer catching up, debounce flicker, a NaN DHT reading, and the
  buzzer's own beep not counting as a distraction.
- ESP32 build: 921,727 bytes (70 % of flash).

## Power fault: wiring everything at once

The guide says to wire one phase at a time and pass each gate before the
next. Instead, the whole board (phases 3–5) was wired in one go from the
wiring diagram and plugged in.

- **Symptom:** the ESP32's red power LED went bright, then dim, then off.
  After the ESP32's power jumpers were pulled, the board showed no LED at all.
  Nothing felt hot.
- **The diagram was not the problem.** It was checked wire by wire and is
  electrically correct.
- **Suspects:** the laptop shutting off its USB port after a short, a dead
  3.3 V regulator (AMS1117), or a bad cable. No multimeter was available.

## 31 Oct: diagnosis and step-by-step rebuild

**Board inspection.** Photos of the board (a 38-pin USB-C ESP32 with a
CP2102 and an AMS1117-3.3) showed no burn marks on the regulator, the input
diode or the capacitors. On this board the 5 V pin is printed **V5**, not
VIN.

**Is the chip alive?** After a laptop restart, the board came back as COM5.
`esptool chip-id` got an answer from the chip: ESP32-D0WD-V3, rev 3.1. The
power LED, though, stayed dark.

**Is the power supply healthy?** A test sketch ran Wi-Fi scans at full
transmit power. That's the heaviest load the regulator sees, and a weak
regulator would cause brownout resets. Result: 11 scans in 45 s, no resets,
no brownout. **Conclusion:** the board survived. The laptop most likely
cut USB power when the short happened. The red LED itself burnt out, which is
cosmetic.

**Finding the short: one wire group at a time.** Since the LED could no longer
show power, the test sketch's uptime counter took its place. After each group
of wires went on, the counter had to keep climbing with no resets.

| Step | Wires added at the ESP32 | Result |
|---|---|---|
| 1 | GND | ✅ no resets |
| 2 | 3V3: OLED, buzzer, DHT11, LM393 power | ✅ no resets |
| 3 | GPIO 21, 22 (I2C) | ✅ I2C scan found the OLED at `0x3C` |
| 3b | (OLED text sketch) | ✅ "DeskPulse 2.0" readable |
| 4 | GPIO 25, 26, 27, 32, 33 (LEDs), 19 (buzzer) | ✅ LEDs chase blue→red, buzzer clicks |
| 5 | V5: 5 V rail for PIR and IR | ✅ no resets |
| 6 | GPIO 23 (PIR), 34 (sound), 16 (DHT11) | ✅ see readings below |
| 7 | GPIO 18 (IR) | ❌ IR stuck at 0, fixed below |

The short never came back. Whatever caused it was undone when the wires were
pulled and re-seated.

**Phase 5 readings:**
- **DHT11:** 27.6–29.3 °C, 64–70 %.
- **Sound (LM393, peak-to-peak):**
  - quiet room: 0–69
  - claps: 600 to 4095 (the maximum)
- **PIR:** 0 at rest once warmed up (about 60 s). It occasionally blips about
  every 4 s with Wi-Fi active, possibly radio interference. Being watched.

**The buzzer is active-HIGH.** It clicked rather than sounding non-stop, so the
firmware's `BUZZER_ACTIVE_LOW = false` is correct.

### The IR divider problem

With the IR module on 5 V and the guide's 1 kΩ + 2 kΩ divider in front of
GPIO 18, the pin read **0 all the time**. The module's red obstacle LED
**glowed faintly even with nothing in front of it**, and brightened when a
hand came near.

- **Hypothesis:** the module doesn't drive its output to 5 V. It only holds it
  HIGH weakly through about 10 kΩ, so the 3 kΩ divider to GND acts as a heavy
  load. The idle output sags to about 2.5 V, leaving about 1.7 V at GPIO 18,
  which the ESP32 reads as 0. Current flowing into the divider also lights the
  obstacle LED faintly.
- **Test:** pull R1 out, which disconnects the divider from the module.
- **Result:** the faint glow disappeared, and the LED lit only for a hand.
  Hypothesis confirmed.
- **Turning the sensitivity pot cannot fix this.** The pot sets the
  detection distance, not how strongly the output is held HIGH.

**Fix:** run the IR module from **3V3** and wire OUT straight to GPIO 18,
with no divider. At 3.3 V the output can't exceed 3.3 V, so it's safe.

- **After:** IR = 1 when clear, 0 with a hand. ✅
- **Trade-off:** range dropped to about **10 cm**. The emitter is dimmer at
  3.3 V, which is why 1.0 ran it at 5 V. That still meets the gate; the sensor
  will be mounted 8–10 cm from the torso.
- **Why not a better divider?** A high-value one (about 47 kΩ / 100 kΩ) would
  allow 5 V. Only 220 Ω, 1 kΩ and 2 kΩ resistors were on hand, and all are
  far too small.

The guide, the wiring diagram and the README were updated to match.

## 31 Oct: phase 6 gate on the real board

**Blynk setup.** The 1.0 template ("Study Monitor") was reused, then checked
and completed:
- **Already there:** datastreams V0–V9.
- **Fixed:** units and defaults.
- **Missing, so added:**
  - the `break_time` event
  - V10–V15 and the `eye_rest` and `session_summary` events, for later phases
- **Sensitivity (V6):** the value stored on the device was **110**, the noise
  margin tuned in 1.0. The firmware loads it from Blynk on every connect.

**First boot of the real firmware:**
```
Calibrating sound: stay quiet for 2 s...
Wi-Fi up (status 3)
Blynk connected   (ping 49 ms)
[AWAY      ] IR=1 PIR=0 sit=0/25 ... wifi=1 blynk=1
```

**Gate tests** were run in `TEST_MODE`, where 1 app minute = 1 s: a 25 s sit
limit and a 5 s break. Evidence came from a timestamped serial log plus the
Blynk dashboard.

| # | Test | Evidence | Result |
|---|---|---|---|
| 1 | Sit in front of the IR | `>> mode: STUDYING`; sit timer counts; V7 At Desk on | ✅ |
| 2 | Keep sitting past 25 s | `>> mode: BREAK TIME` + `>> break due` at 25/25, event fired once | ✅ |
| 3 | Leave for 5 s+ | `ON BREAK` → `AWAY` after 5 s; sit timer back to 0; dashboard 0 % | ✅ |
| 4 | Clap seated, then clap while away | `distraction #1`, `#2` (peak 4095); the away clap added nothing; D = 2 exactly | ✅ |
| 5 | Mute on, then off (V5) | Silent at BREAK TIME while muted; double-beep returns when unmuted | ✅ |

**Also checked:**
- **The V9 Reset button** zeroes the day's counters: `D=0 skip=0 brk=0` in the
  log.
- **The buzzer's own beeps** never counted as distractions: D stayed at 2
  through a whole break alert.
- **No resets or disconnects** during the 4-minute logged run.

**Small notes:**
- **A skipped break is counted one app-minute early in `TEST_MODE`.** The
  grace period counts the alert second as its first second. In real minutes
  that's 1 second out of 5 minutes, so it was left as is.
- **Serial reading tip:** opening the port with RTS toggling resets the
  board and causes boot-loop noise in the log. Listen passively instead.

**Still to confirm by eye:**
- the LEDs fill while studying and blink at BREAK TIME
- exactly one push notification arrives per break

## 31 Oct: phase 7, OLED status screen (Enhancement 1)

**Design:** the screen is split in two, so the part that can be wrong is the
part that gets tested.
- **`screen.cpp` (pure logic, host-tested)** turns the app state into exactly
  what the screen shows. That covers:
  - the status word and the timer row
  - the bar width
  - the environment, distraction and focus text
  - the link state, and burn-in dim/off
- **`display.cpp` (ESP32 only)** just draws that with the Adafruit SSD1306
  library. It checks that something answers at `0x3C` before calling
  `begin()`, because the library's `begin()` doesn't check, and an unplugged
  OLED would otherwise look like a working one.

**Tests first.** 15 new host tests were written before the code, and they
failed against an empty stub (44 failed checks). They cover:
- every mode word, including OBJECT? (and that it only replaces AWAY)
- "Left" reaching 00:00 on the exact tick the break alert starts, and never
  going negative
- the ON BREAK countdown ending on the tick the break is counted
- `mm:ss` capped at 99:59, so a two-hour ignored alert still fits the
  21-character row
- a zero sit limit (no divide by zero)
- a missing DHT reading, or half of one
- D: up to 65535
- "Focus --" before there's a score
- Online / No cloud / Offline, including a stale cloud flag with Wi-Fi down
- the eye-rest overlay
- burn-in: dim when away, off after the session ends, awake on sit-down

**Result:** 56 tests, 459 checks, 0 failures. The ESP32 build is 958,923
bytes (73 %), with no warnings.

**On the board:** three full cycles were recorded with no resets, and Wi-Fi
and Blynk stayed connected throughout:
```
05:46:11.4 >> mode: STUDYING
05:46:35.4 >> mode: BREAK TIME   (sit 25/25)
05:46:40.4 >> mode: ON BREAK
05:46:44.4 >> mode: AWAY         (break counted after 5 s)
05:46:49.5 >> session ended      (screen off)
05:47:09.4 >> mode: STUDYING     (screen wakes)
```

| Gate check | Status |
|---|---|
| Every mode shows its word | ✅ STUDYING, BREAK TIME, ON BREAK, AWAY on the board · ⚠️ OBJECT? host-tested only |
| "Left" hits 00:00 as the LEDs fill and the alert starts | ✅ on the board, plus a host test |
| ON BREAK countdown ends exactly when the break counts | ✅ on the board (4 s from ON BREAK to AWAY), plus a host test |
| Wi-Fi off shows Offline; timers keep running | ⚠️ host-tested only, not run on hardware |
| Boots with the OLED unplugged | ⚠️ not run yet (code path: probe fails, Serial warning, no-op draws) |

The three ⚠️ items are cheap to check later on the board. Run them before
the demo.
