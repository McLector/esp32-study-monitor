# DeskPulse 2.0: build log

What actually happened while building DeskPulse 2.0, phase by phase: what was
tried, what broke, how it was diagnosed, and the evidence each gate passed on.
The plan itself is in [`rebuild-guide.html`](rebuild-guide.html). This file is
the record of carrying it out. Add a new entry each time a gate passes or
something goes wrong.

| Phase | Gate | Result | Date |
|---|---|---|---|
| 01 Toolchain and template | Every library compiles | ✅ passed | Oct 2026 |
| 02 Bare ESP32 | Correct Manila time within 10 s | ✅ NTP time matched the PC clock (in the phase 9 firmware) | 31 Oct 2026 |
| 03 I2C bus and OLED | Scanner finds `0x3C`; text readable | ✅ passed | 31 Oct 2026 |
| 04 Outputs | LEDs chase 1→5; buzzer clicks | ✅ passed | 31 Oct 2026 |
| 05 Inputs | IR 1 idle / 0 at 10 cm; PIR 0 at rest; clap spikes; DHT plausible | ✅ passed, after an IR wiring change | 31 Oct 2026 |
| 06 Core firmware port | 1.0's five regression tests | ✅ passed | 31 Oct 2026 |
| 07 OLED status screen | Every state shows its screen; timers tick in real time | ✅ happy path on the board; 3 checks not run on hardware | 31 Oct 2026 |
| 08 Configurable Pomodoro | Slider changes take effect at once and survive a reboot | ✅ both sliders apply within 1 s; counters survive resets. Power cycle without Wi-Fi: out of scope, unit-tested | 31 Oct 2026 |
| 09 Focus score + rollover | The five score rows reproduce; counters survive a power cycle | ✅ rows reproduce (unit tests); live score matches hand calculation; midnight rollover unit-tested only | 31 Oct 2026 |
| 10 Eye-rest | Overlay at 20 s with a 25 limit, never with 20 | ✅ both, on the board | 31 Oct 2026 |
| 11 Session summary | Exactly one summary per session, "1 break" | ✅ 13 summaries, all correct, incl. 1 and 2 breaks | 31 Oct 2026 |
| 12 Integration and budget | Full test plan; ≤ 500 messages in a real hour | ⏳ happy-path tests done; real-time hour not run | — |

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

### Dashboard: Sit Progress lagged behind the board

- **Symptom:** after a session ended, the Sit Progress gauge (V8) stayed high
  for a while before catching up.
- **Cause:** V8 was only sent every 30 s, as the guide's message budget
  planned. In `TEST_MODE` a whole session lasts 25 s, shorter than one
  update, so the gauge always looked a cycle behind.
- **Fix:** V8 is now also sent on every mode change. That's a few extra
  messages an hour, so the budget is unchanged.
- **Verified:** the board's serial log and the Blynk HTTP API (polled once a
  second) were recorded side by side:

| Board | Blynk | Delay |
|---|---|---|
| 06:02:16.1 BREAK TIME | V8 = 100 | 0.8 s |
| 06:02:25.2 AWAY (break counted) | V8 = 0 | 0.4 s |

While you're seated, V8 still updates every 30 s, which is smooth at real
speed.

**Blynk "Chart Copy" widget:** a 1.0 leftover linked to nothing. It's now
"Daily Focus" (V15, bar chart), ready for phase 9.

## 31 Oct: phases 8–11 built (Enhancements 2–4)

**Tests first, as before.** Each phase's tests were written against empty
stubs and failed first: 64 failing checks for phase 8, then 50 for phases
9–11. All of them pass now: **97 tests, 659 checks, 0 failures**, no compiler
warnings. The ESP32 build is 996,457 bytes (76 %).

**Phase 8: configurable Pomodoro.** V11 (sit limit) and V12 (break length) are
set from Blynk sliders. They're saved in NVS along with mute, sensitivity, the
eye-rest switch and today's counters. A fake `Preferences.h` lets the real
`storage.cpp` run in the host tests. The tests cover:
- out-of-range values clamped *before* narrowing to a byte (300 would
  otherwise become 44)
- a corrupt saved sit limit of 0 or 255 clamped on load
- a wrong-size saved copy rejected
- TEST_MODE counters never loading into the real build
- no flash writes when the dashboard re-sends the same values on every
  reconnect
- a failed write retried 60 s later

**A state machine change.** Raising the sit limit during BREAK TIME used to
leave the alert beeping while the screen showed time left. Now it goes back
to STUDYING and warns again at the new limit. A skip that was already counted
stays counted. Whether a break was "on time" is judged from when the alert
fired, so changing the limit mid-break can't flip it.

**Phase 9: focus score and daily rollover.** The guide's formula and its five
example rows are host tests. NTP gives Manila time. At midnight the final
score goes to V15 and today's counters reset. V10 sends the score every 60 s.
In TEST_MODE you can type `t=120 d=6 s=1` into the Serial Monitor to check a
score by hand.

**Phase 10: eye-rest.** The guide's rules are implemented: three light
chirps (silent when muted), one LED sweep, the OLED overlay and the `eye_rest`
event. V13 turns it off.

**Phase 11: session summary.** When a session ends, the board sends one
`session_summary` event. The final departure is not counted as a break.
Sessions under 5 study minutes send nothing. If Blynk is offline, the summary
waits and is sent on reconnect.

### Crash at first boot: NTP vs Blynk DNS

- **Symptom:** the first boot after uploading the phase 9 build crashed with
  `assert failed: udp_new_ip_type ... Required to lock TCPIP core
  functionality`, then rebooted fine.
- **Diagnosis:** the backtrace, decoded with `addr2line`, showed:
  1. Blynk's connect calls `NetworkManager::hostByName()` (ESP32 core 3.3.11).
  2. When the IP state changes, that function clears the DNS cache **without
     taking the lwIP lock**.
  3. Clearing the cache fired the NTP client's lookup of `pool.ntp.org`,
     which was still waiting.
  4. That callback opened a UDP socket unlocked, so lwIP asserted.
- **Risk:** this would happen at random, at boot or after any Wi-Fi reconnect.
- **Fix:** NTP is given Google Public NTP's literal IPs, so it never has a
  DNS lookup waiting.
- **Verified:** uploaded and then booted 4 times. 0 crashes; Blynk connected
  and NTP set the time on every boot. Today's counters
  (`D=7 skip=3 brk=6`, focus 46) came back after every reset.

**Still to run on the board:** the slider and power-cycle checks (with Wi-Fi
off), the eye-rest overlay at 20 s, the summary push, and the earlier
by-eye checks.

## 31 Oct: phases 8–11 on the board

These checks were run in `TEST_MODE` (1 app minute = 1 s) with a timestamped
serial log and the Blynk dashboard.

**A rule change from testing.** Claps made while still seated during BREAK
TIME were counted as distractions. Once the alert fires, the study block is
over, so distractions now count only in STUDYING (`c832a43`).

| Check | Evidence | Result |
|---|---|---|
| V11 changed mid-session | `>> sit limit 60 min` at 06:35:47; status `sit=0/60` the next second; BREAK TIME at 60; saved 52 s later | ✅ |
| V12 changed mid-session | `>> break length 8 min` at 07:39:01; countdown `/8` within 1 s; break counted after 8 s | ✅ |
| Sit limit 20 | BREAK TIME at 20/20, no eye-rest before it | ✅ |
| Early return during BREAK TIME | Left at sit 28, back after 2 s: BREAK TIME again, sit kept | ✅ |
| Skipped break | `skip` +1 once per cycle; focus fell 91 → 76 (exactly 15) | ✅ |
| Eye-rest | Fired at sit 20 all 13 times; never in BREAK TIME or in a 19 s sitting | ✅ |
| Session summary | 13 summaries, every figure matched the log; e.g. "You studied 67 min, took 1 break, had 2 distractions. Focus today: 78." | ✅ |
| V9 Reset | Counters to 0 and saved | ✅ |

**Blynk events were capped at one per hour.** All three events had a Limit
Period of once per hour, so every break reminder after the first in an hour
was being dropped. That's probably why "exactly one notification per break"
could never be confirmed. It's now once per second.

**Out of scope for hardware testing** (unit-tested only): settings surviving
a power cycle with Wi-Fi off, the Offline screen, and the OBJECT? guard. Phase
12's real-time message-budget hour has not been run.
