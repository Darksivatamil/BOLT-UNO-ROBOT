# BOLT UNO ROBOT — Test & Calibration Plan (v2.0)

Do these in order. Each step has a pass condition. If a step fails, fix it
before moving on — later steps assume earlier ones work.

> **A robot that is completely dead is a power problem, not a code problem.**
> Phase 1 proves the 5 V rail before anything else is blamed.

---

## Phase 0 — Before any power

- [ ] No battery connected, USB disconnected.
- [ ] **Button is on A3, other leg to GND** (`INPUT_PULLUP`, no resistor).
      Pin 8 does **not** work: this shield holds it LOW with nothing connected.
      Use opposite-side legs on a 4-pin tact switch.
- [ ] Multimeter continuity: **5 V rail to GND must NOT beep.**
- [ ] Every module's GND is connected to the same ground point.
- [ ] HC-05 RX has the 1k/2k divider fitted.
- [ ] Speaker is on L+ / L−, **not** on GND.
- [ ] Nothing is plugged into pins 3,4,5,6,7,8,11,12 (shield only).
- [ ] A **5 V 3 A buck/UBEC** is in the power path (see `WIRING.md` §3).
      The 3S pack (~11–12 V) must never reach the Uno's 5 V pin directly.

---

## Phase 1 — Power (do this before uploading anything)

Battery connected, **USB unplugged**, robot switched on.

1. Multimeter on the Uno's **5 V pin** to GND.
2. Multimeter on the shield's 5 V rail.

**Pass:** both read **4.9–5.1 V** and stay there when the servo moves.

If you read 3.0–4.2 V or 11–12 V: the 5 V buck is missing, bypassed, or set
wrong.
If you read 0 V: main switch, fuse, BMS, or a missing ground.
**Do not continue until this is 5 V.**

> Set the buck to 5.0 V **with no load** before connecting the Uno. Above
> ~5.5 V you will damage the board.

### Phase 1b — No buck yet? bench-test with 5 V 2 A

If you do not have the buck, you can still prove the whole robot today with a
**5 V / 2 A phone charger or USB power bank**:

1. Remove the shield's **PWR jumper**.
2. Charger USB → Uno USB socket.
3. The same charger's 5 V → `EXT_PWR +`, and its GND → `EXT_PWR −` **and** the
   Uno GND (common ground).
4. One supply only — do not connect the battery at the same time.

Measure 4.9–5.1 V at the Uno 5 V pin. This is enough for every test in this
plan with the wheels off the ground. See `WIRING.md` §3.4.

---

## Phase 2 — Hardware diagnostic (`BOLT_TEST`)

This sketch tests each part separately and prints the result. Use it whenever
something does not react.

1. **Switch the battery off and unplug the buck output.**
2. Unplug the HC-05 TX/RX wires (they block uploads on pins 0/1).
3. Open `BOLT_TEST/BOLT_TEST.ino` and upload it.
4. Open **Serial Monitor at 9600 baud**.
5. Put the robot on a stand so the wheels can spin freely.

Expected checklist:

| Step | Expected | If it fails |
|------|----------|-------------|
| button pin | "A3 to GND ... reads HIGH" note, no held warning | if it says LOW, release the button / check wiring |
| [1/6] LED | 3 slow blinks | board not running / power |
| [2/6] audio | 3 rising tones | PAM8403 power, A2 wire, volume pot |
| [3/6] servo | right → ahead → left | servo power/signal, SER1 header |
| [4/6] ultrasonic | 8 distance readings, at least 5 valid | A0/A1, 5 V, GND |
| [5/6] motors | M1, M2, M3, M4 one at a time, then all four | motor wiring, shield power |
| [6/6] button | press the A3 button within 10 s → "button OK" | button wiring, A3 to GND |

After the checklist the sketch prints live status every 0.5 s:

```
dist=24cm  button=up
```

* Wave your hand in front of the sensor: `dist` must change.
* Press the button: `button` must change to `DOWN`.

**Pass:** every line is OK and the live status tracks your hand and the button.

---

## Phase 3 — Upload the main firmware

1. Keep the HC-05 TX/RX unplugged.
2. Arduino IDE → Tools → Board: **Arduino Uno**, Port: your port.
3. Open `BOLT_UNO_ROBOT/BOLT_UNO_ROBOT.ino`, click Upload.
4. Open Serial Monitor at **9600** (module still unplugged).

**Pass:** "Done uploading"; the monitor prints:

```
BOLT v2.0 READY
BTN:A3-GND  PIN8=motor data, never a button
```

Sketch size ≈ 10.8 KB (34 % of flash).

5. Switch off, reconnect the HC-05, switch on.

> Upload fails with "avrdude: stk500_recv()"? The Bluetooth module is still on
> pins 0/1. Unplug it, or set `BT_USE_SOFTWARE_SERIAL 1` and move the module
> to A4/A5.

---

## Phase 4 — Self test (proves the drive wiring)

1. Hold the button down.
2. Switch the robot on.
3. **Release it when you hear the first beep.** The firmware waits briefly for
   the release before moving the motors (so a still-held button is not mistaken
   for a mode change). If you keep holding for 8 s it skips the motor part and
   beeps a low tone.

Expected sequence:

| Step | What happens | If it does not |
|------|--------------|----------------|
| beeps | two short beeps | PAM8403 power, A2 wire, volume pot |
| M1 | left-front wheel forward 0.6 s | motor wiring / `INV_M1` |
| M2 | left-rear wheel forward 0.6 s | motor wiring / `INV_M2` |
| M3 | right-front wheel forward 0.6 s | motor wiring / `INV_M3` |
| M4 | right-rear wheel forward 0.6 s | motor wiring / `INV_M4` |
| both | all four forward, then all four back | one side inverted → see calibration |
| servo | sensor looks right → ahead → left | swap `SIDE_RIGHT_ANGLE` / `SIDE_LEFT_ANGLE` |
| end | rising two-note chirp | — |

**Then it automatically continues into the normal boot animation.**

If a *forward* command drives a wheel *backwards*, set that motor's `INV_Mx`
to `1` (or swap its two wires at the terminal).

---

## Phase 5 — Boot / loading

Power on normally (button not pressed).

**Pass:**
- A rising 8-bit melody plays for ~2.6 s.
- The servo sweeps fully right-left-right for 3 s.
- Both stop together, the servo centres, and the robot starts moving forward
  in obstacle mode.
- The on-board LED blinks **once** every 2.4 s.

**If it does not move:** listen for the 2.4 s low sensor-fault warning and
watch for fast LED flicker. That means the HC-SR04 is not answering — the
robot deliberately refuses to drive blind. Check A0/A1/5 V/GND, or run
`BOLT_TEST`.

---

## Phase 6 — Obstacle mode

Place a wall or box in front.

| Test | Expected |
|------|----------|
| Open floor, wall within 4 m | drives straight, no beeping |
| Wall at ~15 cm | stops, **one beep**, servo looks right then left, turns toward the free side, drives on |
| Wall + box on the right only | looks right (blocked), looks left (clear), turns **left** |
| Wall + box on the left only | turns **right** |
| Boxes both sides | one low beep, reverses ~0.7 s, spins ~180°, drives on |
| Remove the obstacle | it keeps driving, no repeated beeps |
| Unplug the HC-SR04 while driving | stops, plays the 2.4 s warning, LED fast-flickers |
| Plug the sensor back in | short high beep, resumes |

**Pass:** never touches the obstacle, the side choice follows the rule
*"blocked side → go the other way"*, and it never drives when the sensor is
silent.

> The HC-SR04 reliably sees ~4 m. If your test room has nothing within that
> range, the robot stops (correctly). Use a smaller area, or set
> `NO_ECHO_MEANS_CLEAR 1` only if you accept the risk.

---

## Phase 7 — Bluetooth mode

1. Short-press the button once. LED blinks **twice**, short chirp.
2. Pair the phone with HC-05/HC-06 (PIN `1234` or `0000`).
3. Open **"Bluetooth RC Car"** (or any RC car app that sends `F B L R S I J G H`).
4. The app must send **single characters**. The firmware also accepts whole
   words (`forward`, `back`, `left`, `right`, `stop`, ...) from a serial
   terminal.

| App button | Command | Robot |
|-----------|---------|-------|
| ▲ | `F` | forward |
| ▼ | `B` | backward |
| ◀ | `L` | spin left |
| ▶ | `R` | spin right |
| ▲◀ | `G` | forward-left arc |
| ▲▶ | `I` | forward-right arc |
| ▼◀ | `H` | back-left arc |
| ▼▶ | `J` | back-right arc |
| release | `S` | stop |
| speed 1–9 | `1`–`9` | speed changes |

**Pass:** reacts within ~0.15 s, stops when the app is released, and the robot
stops by itself if the phone goes out of range (failsafe 1.2 s).

> If your app sends another protocol, enable `ENABLE_DEBUG` **in
> SoftwareSerial mode** and see what arrives, then add it to
> `handleBtCommand()` or `btHandleWord()`.

### Emergency stop (test this once, on the floor)

Hold the button for ~1.4 s while the robot is driving.

**Pass:** motors stop immediately, a descending halt tone plays, the LED does
a rapid double blink. Press once to resume the same mode.

---

## Phase 8 — AI mode

Short-press the button again. LED blinks **three times**, different chirp.

**Pass:**
- Clearly slower than obstacle mode.
- Accelerates and brakes gradually (no jerking).
- The servo sweeps in small, slow steps.
- In front of a narrow gap it eases off instead of stopping hard.
- If the front is blocked, it scans 5 directions (right, 45°, front, 135°,
  left) and turns toward the widest gap.
- If boxed in: low tone → reverse → turn → try again.
- Occasional short "thinking" beeps, and a chirp when it finds a clear path.

Press again → back to obstacle mode, LED blinks once.

---

## Calibration (do this once, on the floor)

### 1. Turn angle — `TURN90_MS`
Default `520` ms at speed `175`.

Mark a 90° turn on the floor with tape:

1. Put the robot on the line.
2. Cover the sensor so it triggers a turn.
3. Measure how far it actually turned.

More than 90° → **decrease** `TURN90_MS`. Less than 90° → **increase** it.
Typical range: 380–750 ms, depending on the floor and battery charge.

### 2. Servo direction
If the sensor looks **left** when the code says right, swap:
```c
#define SIDE_RIGHT_ANGLE   0    ->  180
#define SIDE_LEFT_ANGLE    180  ->  0
```
(and swap `SCAN_45_RIGHT` / `SCAN_45_LEFT` too: 45 ↔ 135).

### 3. Stop distance — `OBSTACLE_STOP_CM` (default 15)
Increase to 20–25 for a fast robot on a smooth floor. The robot needs braking
room; if it "kisses" walls, raise this first.

### 4. Speeds
| Constant | Effect |
|----------|--------|
| `OBSTACLE_SPEED` 190 | obstacle-mode cruise |
| `BT_MAX_SPEED` 255 | bluetooth top speed |
| `AI_CRUISE_SPEED` 135 | AI cruise (keep below obstacle) |
| `RAMP_STEP_AI` 7 | higher = snappier AI, lower = smoother |

### 5. Sensor reliability
If the robot randomly stops in open space:
- add the 100 nF motor capacitors,
- keep the HC-SR04 wires away from the motor wires,
- lower `OBSTACLE_SPEED`,
- check the 5 V rail does not sag when the motors start (Phase 1).

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| **Completely dead at power-on** | 5 V buck missing, or 3S pack on the 5 V pin | §3 of `WIRING.md`; measure 5 V pin |
| Upload fails | HC-05 on pins 0/1 | unplug module, or use SoftwareSerial |
| Board resets when motors start | buck too small / no bulk capacitor | 5 V 3 A buck, 1000 µF + 100 nF |
| No sound | PAM not powered / volume at 0 / A2 wire | check 5 V, turn the pot up |
| Sound but motors stutter | amp drawing from a weak 5 V rail | bigger capacitor, separate 5 V |
| One side drives backwards | motor polarity | set `INV_Mx = 1` |
| Robot spins in circles | one side inverted | run the self test |
| Servo jitters / resets | servo powered from USB | power servo from the 5 V buck rail |
| Distance always 999 | TRIG/ECHO swapped or sensor unpowered | check A0/A1, 5 V |
| Robot stops + 2.4 s low beep, LED flickers | ultrasonic fault (by design) | check A0/A1/5 V/GND, run `BOLT_TEST` |
| Stops after 1.2 s in BT | app does not repeat commands | that is the failsafe; use an app that repeats, or raise `BT_FAILSAFE_MS` |
| Button does nothing | button not to GND, or wrong side of a 4-leg switch | A3 → button → GND, use opposite-side legs |
| Button always reads pressed | two same-side legs of a 4-leg switch | move one leg to the opposite side |
| Button on pin 8 never works | shield holds pin 8 LOW (measured) | use A3 |
| Mode changes twice per press | long-press timing | use a short press; 0.9–1.4 s is deliberately ignored |
| Turns too much / too little | `TURN90_MS` | calibrate above |
| Bluetooth does nothing | wrong baud / not paired / TX-RX not crossed | 9600, pair 1234, module TX→pin 0 |
| Random resets when motors start | 5 V rail sags / no bulk capacitor | 1000 µF, stronger 5 V buck |

---

## What "correct" looks like

- Boots with music and a 3 s scan sweep, then drives.
- Stops at 15 cm, beeps once, scans both sides, picks the free side.
- Boxed in → reverses and turns around.
- Unplugging the sensor stops it and beeps — it never drives blind.
- One short press → BT, twice → AI, three times → obstacle.
- 1.4 s hold → emergency stop; press again → resume.
- BT reacts instantly and stops on release and on signal loss.
- AI is slower, smoother, and takes the wider gap.
