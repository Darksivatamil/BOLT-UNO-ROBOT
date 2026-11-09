# BOLT UNO ROBOT

Obstacle-avoiding + Bluetooth + AI-mode robot on an Arduino Uno with an
L293D (HW-130) motor shield. One button cycles the three modes.

```
        ┌──────────────────────────────────────────────┐
        │  OBSTACLE  ──button──►  BLUETOOTH  ──button──►│
        │      ▲                              AI        │
        │      └──────────────button───────────────────┘│
        └──────────────────────────────────────────────┘
```

---

## Files

| File | What it is |
|------|------------|
| `BOLT_UNO_ROBOT/BOLT_UNO_ROBOT.ino` | Complete firmware v2.0. One file, one library (`Servo`). |
| `BOLT_TEST/BOLT_TEST.ino` | Hardware diagnostic. Upload this **first** when something is dead; prints a checklist at 9600 baud. |
| `BOLT_BTN/BOLT_BTN.ino` | Button-only test: on-board LED mirrors the press on A3. |
| `WIRING.md` | Exact pin map, power design, safety notes |
| `TEST_PLAN.md` | Step-by-step bring-up + calibration |
| `UPLOAD.md` | Flash the already-built binary (IDE / CLI / avrdude) |
| `upload.sh` | One-command compile + upload helper |

**Read `WIRING.md` first.** The two things that stop this robot from working are
**no 5 V regulator** and **audio on pins 10/11**. The button had to move off
pin 8 (that shield pin reads LOW permanently) and now lives on **A3**.
Everything was verified on the actual board.

---

## Quick start

0. **Fix the power first.** 3S pack (~11–12 V) → **5 V 3 A buck/UBEC** →
   Uno **5V pin**. Never put the 3S pack on the 5 V pin, and never charge it
   with a TP4056 (1S only). Set the buck to 5.0 V before connecting.
   **No buck yet?** Bench-test with a 5 V/2 A charger: Uno USB + `EXT_PWR`
   (PWR jumper off), common ground. See `WIRING.md` §3.4.
1. Wire it exactly as `WIRING.md` says. **Button on A3, other leg to GND**
   (`INPUT_PULLUP`, no resistor). Do **not** use pin 8 — this shield holds it
   LOW even with nothing connected.
2. Install **Servo** (Sketch → Include Library → Manage Libraries → "Servo").
3. Upload with the Bluetooth module's TX/RX unplugged.
4. If anything is dead, upload `BOLT_TEST/BOLT_TEST.ino` first and read the
   checklist at **9600 baud**. It tests the LED, audio, servo, ultrasonic,
   motors and button one at a time.
5. Hold the button while powering on, **release at the first beep** →
   hardware self test.
6. Power cycle normally → boot music + 3 s scan sweep → obstacle mode.

Compiles clean with `arduino-cli compile --fqbn arduino:avr:uno --warnings all`:

```
Sketch uses 11108 bytes (34%) of program storage space.
Global variables use 716 bytes (34%) of dynamic memory.
```

### Button

| Action | Result |
|--------|--------|
| short press (release under 0.9 s) | next mode (1 → 2 → 3 → 1) |
| hold 1.4 s | **emergency stop**; hold again to resume |
| hold at power-on, release on the first beep | hardware self test |
| release between 0.9 s and 1.4 s | ignored (prevents accidental mode changes) |

The LED blinks the mode number: 1 = obstacle, 2 = Bluetooth, 3 = AI.
Emergency stop = rapid double blink. Ultrasonic fault = fast flicker.

---

## Modes

### 1. OBSTACLE — default after boot
Fast, deterministic, no hesitation.

* Drives forward, measuring the front 3× per reading (median filtered).
* At **45 cm** it eases off to a slower approach speed, so it has braking
  room and never kisses the wall.
* At **15 cm**: stop → **one beep** → servo right 90° → measure → servo
  left 90° → measure → back to centre.
* Right blocked → **go left**. Left blocked → **go right**.
* Both clear → take the wider gap.
* Both blocked → **reverse 0.7 s, spin ~180°, continue**.
* After turning it re-checks the new heading and escapes if still blocked.
* **No echo is never treated as "clear".** If the HC-SR04 stops answering,
  the robot stops, plays a 2.4 s warning, and fast-flickers the LED. It
  recovers automatically when the sensor answers again.
* If nothing is within ~4 m (large room), it also stops. Set
  `NO_ECHO_MEANS_CLEAR 1` only if you accept that risk.

### 2. BLUETOOTH — one button press
Manual control, **255 PWM** (the whole point: it is faster).

Supports the standard **"Bluetooth RC Car" / "Bluetooth RC controller"**
protocol, so any app that sends these characters works:

```
F forward   B back   L left   R right   S stop
G fwd-left  I fwd-right  H back-left  J back-right
0..9 speed   q max speed
```

It also accepts whole words (case-insensitive): `forward`, `back`,
`backward`, `reverse`, `left`, `right`, `stop`, `forwardleft`,
`forwardright`, `backleft`, `backright`.

* 1.2 s failsafe: no command → stop (out of range = safe).
* Optional collision guard (`BT_COLLISION_GUARD 1`).
* No music or beeps while driving.

### 3. AI — two button presses
Slow, smooth, "human" driving.

* 3-zone continuous cruise with **proportional differential steering**: it
  curves away from the nearer side instead of driving into it.
* Speed **eases down proportionally** from 60 cm, instead of braking hard.
* Below 18 cm: stop → "thinking" beeps → **5-zone scan**
  (−90°, −45°, 0°, +45°, +90°).
* Picks the side with the widest weighted gap, with **momentum** so it does
  not dither between two equal sides.
* Boxed in: low tone → reverse → pivot → second chance pivot.
* Sensor fault during a scan: stop immediately, never trust five 999s as
  "wide open space".
* Everything is ramped at 7 PWM per 20 ms and the servo moves 3° per 5 ms,
  so motion and scanning are smooth.
* Custom 8-bit cues: thinking, clear path, blocked, backing up.

---

## Sound design

All audio is a **software square wave** on A2 → PAM8403. No `tone()`.

> Why: on the Uno, `tone()` takes over Timer2. Timer2 drives the PWM outputs
> for M1 (pin 11) and M2 (pin 3) on this shield, so every beep would make the
> left wheels stutter. The custom engine toggles a GPIO from `micros()` and is
> even serviced *inside* the ultrasonic echo wait, so music never glitches.

| Event | Sound |
|-------|-------|
| Power on | ~2.6 s rising 8-bit melody while the servo scans |
| Mode change | short 3-note chirp (can be disabled with `MODE_CHANGE_CHIRP 0`) |
| Obstacle found | single 1200 Hz beep |
| AI thinking | 4 fast notes |
| AI path clear | rising 2-note chirp |
| AI boxed in / reverse | low descending notes |
| Ultrasonic fault | 2.4 s low warning; robot is stopped |
| Emergency stop | descending 3-note halt |
| Sensor recovered | short high beep |

---

## Decisions that differ from the original plan

| Original | Built | Why |
|----------|-------|-----|
| Battery straight into the Uno/shield | **5 V 3 A buck/UBEC → Uno 5 V pin** | Pack is 3S = 11.1–12.6 V. The Uno needs 5 V; this is why the motors and audio collapsed. |
| Button on pin 6 / pin 8 | **Moved to A3** | Pin 6 is M3's PWM. Pin 8 is held LOW by this shield even with the wire removed (measured), so it can never detect a press. A3 is exposed on the analog header, free, and needs no resistor. |
| PAM L→10, R→11 | **A2 → 1k → L input**, R→GND | Pin 10 is the servo (Timer1), and pins 10/11 are Timer1 PWM which `Servo` owns. Also the PAM8403 is analog. |
| BT RX→0, TX→1 | Kept **0/1** as you chose | Upload works only with the module unplugged. One-line switch to SoftwareSerial (A4/A5) if you prefer. |
| 3 cells, one TP4056 | **3S series + 3S BMS + 12.6 V charger** | The pack measures ~11–12 V, so it is 3S. A TP4056 is a 1S charger and must never be used on it. |

### What changed in firmware v2.0

* **Never drives blind.** The HC-SR04's own 38 ms timeout tells "nothing in
  range" apart from "sensor not answering". A short dropout = hold still; no
  answer for 2.2 s (at boot) or 0.6 s (after it was working) = fault melody +
  stop. Recovery is automatic on the next answer.
* The median filter uses only valid samples, so two missed pings can no
  longer hide a real 20 cm wall.
* Short press = next mode; 1.4 s hold = emergency stop / resume.
* Bluetooth accepts single characters **and** whole words.
* Compile-time guards reject the button/audio on any motor-shield pin.
* Boot banner confirms the firmware is running (9600 baud).

---

## Tuning in one place

Everything is at the top of the `.ino`, in `SECTION 1`:

```
OBSTACLE_STOP_CM   15      TURN90_MS        520   <- calibrate this
OBSTACLE_SPEED     190     AI_CRUISE_SPEED  135
BT_MAX_SPEED       255     RAMP_STEP_AI     7
INV_M1..M4         0       SIDE_RIGHT_ANGLE 0
BTN_LONG_MS        1400    SENSOR_GRACE_MS  600
NO_ECHO_MEANS_CLEAR 0      SENSOR_BOOT_WAIT_MS 2200
```

`TEST_PLAN.md` explains how to calibrate `TURN90_MS` and the servo direction.

---

## Hardware notes worth knowing

* **Completely dead?** It is almost always power. Measure the Uno's 5 V pin:
  it must read 4.9–5.1 V with the battery connected and USB unplugged. 11–12 V
  or 3.7 V there means the 5 V buck is missing or bypassed.
* **Servo power:** an SG90 stalls at ~700 mA. Power it from the 5 V buck with
  a 1000 µF capacitor, never from the Uno's USB rail.
* **Motor noise:** 100 nF across each motor's terminals prevents most random
  ultrasonic and Bluetooth glitches.
* **L293D drop:** ~1.4 V. With a 5 V rail the motors see ~3.6 V, which is the
  correct rating for TT gear motors.
* **Never** connect USB and the battery at the same time with the shield's
  PWR jumper fitted.
* **Speaker:** BTL output — never connect a speaker terminal to GND.
