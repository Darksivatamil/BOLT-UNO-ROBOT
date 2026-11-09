# BOLT UNO ROBOT — Wiring Guide

Read this fully before connecting power. The original plan had **three**
problems that stop the robot from working; they are marked **CHANGED** or
**CRITICAL** and explained.

> ## ⚠ If the robot is completely dead, it is the power — not the code
> Your pack is **3S (~11–12 V)**, so you need a **5 V 3 A BUCK (step-down)**
> converter, not a boost. The Uno needs a regulated **5.0 V**; never put the
> 3S pack on the 5 V pin. A **TP4056 is a 1S charger and must not be used on a
> 3S pack** — use a 3S BMS + 12.6 V charger. See §3.
>
> ## ⚠ Pin 8 cannot be the button — it is held LOW by the shield
> Measured on this hardware: with **no wire** on pin 8 it still reads LOW.
> The L293D shield's 74HC595 data input network holds the line at ground, so a
> button there can never read HIGH. **The button is on A3** (analog header,
> `INPUT_PULLUP`, no resistor). Pin 2 usually works too but is hidden under the
> stacked shield. See §0 and §2.4.

---

## 0. The three things that stop this robot from working

| # | Problem | Why it fails | Fix |
|--:|---------|--------------|-----|
| 1 | **Motors powered from USB / no 5 V regulator** | 3S = 11–12 V, but the Uno and shield need 5 V. USB gives ~0.5 A: one motor moves, four motors collapse the rail (motors stall **and** audio dies together). | **5 V 3 A buck/UBEC** from the 3S pack → Uno 5 V pin + shield `EXT_PWR` (PWR jumper OFF). §3 |
| 2 | **Button on pin 8** | Pin 8 is the shift-register data line, and this shield holds it LOW permanently (even with nothing connected), so a button there never reads HIGH. | Button leg 1 → **A3**, leg 2 → **GND**. `INPUT_PULLUP`, no resistor. §2.4 |
| 3 | **Audio on pins 10/11** | Pin 10 is the servo signal and Timer1 belongs to the Servo library. | **A2 → 1k → PAM8403 L**. §2.6 |

Everything else in this guide assumes those three are correct.

---

## 1. Pin map (final, tested against the Arduino Uno + L293D shield)

| Uno pin | Goes to | Notes |
|--------:|---------|-------|
| 3  | L293D shield — M2 PWM | shield internal, **do not use** |
| 4  | L293D shield — 74HC595 clock | shield internal |
| 5  | L293D shield — M4 PWM | shield internal |
| 6  | L293D shield — M3 PWM | shield internal — **cannot be the button** |
| 7  | L293D shield — 74HC595 enable | shield internal |
| 8  | L293D shield — 74HC595 data | shield internal — **reads LOW, cannot be a button** |
| 9  | SER2 header | unused, leave free |
| 10 | **SER1 header → SG90 signal** | servo |
| 11 | L293D shield — M1 PWM | shield internal |
| 12 | L293D shield — 74HC595 latch | shield internal |
| 13 | on-board LED | mode indicator |
| 0  | HC-05/06 **TX → Uno RX** | upload conflict, see §5 |
| 1  | Uno TX → HC-05/06 **RX** | needs 1k/2k divider |
| 2  | *alternative* button pin if exposed on your shield | `INPUT_PULLUP`, no resistor needed |
| A0 | HC-SR04 **TRIG** | |
| A1 | HC-SR04 **ECHO** | |
| A2 | **Audio → 1k → PAM8403 L input** | see §2.6 |
| A3 | **Button → GND** | `INPUT_PULLUP`, no resistor — see §2.4 |
| A4 | *optional* BT RX (SoftwareSerial mode) | spare |
| A5 | *optional* BT TX (SoftwareSerial mode) | spare |

### CHANGED #1 — the button is on **A3**, not pin 8
Pin 8 looked attractive because the 74HC595 only samples it on the `SRCLK`
edge, but **this shield holds pin 8 LOW even with nothing connected** (measured
with the wire removed). No firmware can read a permanently grounded pin.

So the button is on **A3**, which is broken out on the shield's analog header
and free (A0/A1 = ultrasonic, A2 = audio). Wiring is `A3 → button → GND`,
`INPUT_PULLUP`, **no resistor**. Pin 2 is electrically identical and also
supported, but is usually hidden under the stacked shield.

The old pin-8 multiplexing code is retained behind `BTN_MULTIPLEXED` so the
sketch still builds safely if `PIN_BUTTON` is set to 8; it will simply never
see a valid press on this shield. Pin 6 still cannot be used (M3 PWM), and the
other shield pins remain forbidden — the build refuses them.

### CHANGED #2 — the speaker is on **A2**, not pins 10 and 11
* Pin 10 is the servo signal (SER1). It cannot also drive the amplifier.
* Pins 10/11 are Timer1 pins. The `Servo` library owns Timer1 — driving audio
  there would break the servo.
* The PAM8403 is an **analog** amplifier. A single square-wave pin into its
  L input is the correct way to get loud, clean 8-bit sound.

---

## 2. Complete connection list

### 2.1 L293D motor shield onto the Uno
Plug the shield straight in. Nothing to wire.

Motor screw terminals (4WD skid-steer):

```
        FRONT
  M1 (left)      M3 (right)
  M2 (left)      M4 (right)
        BACK
```

* **M1 + M2 = LEFT side**
* **M3 + M4 = RIGHT side**

If a wheel spins backwards, either swap that motor's two wires **or** set
`INV_M1` … `INV_M4` to `1` in the sketch. Both work; swapping wires is fine.

### 2.2 HC-SR04 ultrasonic
```
HC-SR04 VCC  -> shield 5V
HC-SR04 GND  -> shield GND
HC-SR04 TRIG -> A0
HC-SR04 ECHO -> A1
```
Mount it on the SG90 horn so that **servo 90° = straight ahead**.

### 2.3 SG90 servo
```
Brown  -> GND
Red    -> 5V   (see §4: give it a solid 5V, not a weak USB port)
Orange -> SER1 signal row (pin 10)
```
The shield's SER1 header already routes signal to pin 10; the + and − rows
are on the 5V/GND rails.

### 2.4 Button (A3, tested working)
```
A3 ---- button ---- GND
```
No resistor. `INPUT_PULLUP` is enabled in software — do **not** connect the
button to 5V.

For a 4-pin tactile switch, the legs are paired in twos (the two on the same
side are permanently joined). Use **two legs on opposite sides** (diagonal),
or the switch reads as always pressed.

> **Do not use pin 8.** On this shield it is held LOW even with the wire
> removed, so it can never detect a press. A3 is on the exposed analog header
> next to a GND pin, which makes it the easiest reliable choice. Pin 2 works
> too if your shield exposes it — change `#define PIN_BUTTON` and rebuild.
>
> To test the button alone, upload `BOLT_BTN/BOLT_BTN.ino`: the on-board LED
> mirrors the press.

### 2.5 HC-05 / HC-06 Bluetooth
```
VCC -> 5V        (the breakout board has its own 3.3V regulator)
GND -> GND
TX  -> pin 0     (module transmits into the Uno)
RX  -> pin 1     through a divider, see below
```
**RX divider (required, the module is 3.3 V logic):**
```
pin 1 ----[1k]----+---- HC-05 RX
                  |
                [2k]
                  |
                 GND
```
This makes 5 V → 3.36 V. Without it you can damage the module.

### 2.6 PAM8403 + speaker
```
Arduino A2 ----[1k]---- PAM8403 L input
PAM8403 R input ------- GND  (through 1k, keeps the unused channel quiet)
PAM8403 GND ----------- Arduino GND
PAM8403 VCC ----------- 5V
Speaker (+) ----------- L+
Speaker (−) ----------- L−
```

For the cleanest sound, put a **1–10 µF capacitor in series** between the 1k
and the L input (A2 → 1k → +cap → L). It blocks the DC level and removes the
power-on pop. A direct 1k connection still makes sound, just less cleanly.

**Do not connect either speaker wire to GND.** The PAM8403 output is
bridge-tied (BTL); grounding a speaker output will destroy the chip.

Add a 100 µF electrolytic across PAM8403 VCC/GND if you hear motor whine.
Keep the audio wire away from the motor wires.

### 2.7 Status LED
None needed — the on-board LED on pin 13 reports the state:

| Pattern | Meaning |
|---------|---------|
| 1 blink / 2.4 s | obstacle mode |
| 2 blinks / 2.4 s | Bluetooth mode |
| 3 blinks / 2.4 s | AI mode |
| rapid double blink | **emergency stop** (long button press) |
| fast flicker | **ultrasonic fault** — robot is stopped, sensor not answering |
| no blink at all | no power / board not running |

---

## 3. Power — the part that decides whether this works

Your pack: **3 × 18650 in series (3S)** — **11.1 V nominal, 12.6 V full.**
The Uno needs a regulated 5 V, so a **5 V BUCK (step-down)** converter is
mandatory. Never feed the 3S pack to the Uno's 5 V pin.

### 3.1 Your pack is 3S — use a 5 V **BUCK**, not a boost

Measured pack: **3 × 18650 in series = 11.1 V nominal (12.6 V full).**

```
  3S pack + 3S BMS
        |
   [ main switch ] [ 3 A fuse ]
        |
        +──── 5 V 3 A BUCK / UBEC ──┬── Uno 5 V pin
        |                           ├── shield EXT_PWR +   (PWR jumper OFF)
        |                           ├── PAM8403 VCC
        |                           ├── servo +
        |                           └── HC-05 / HC-SR04 VCC
        |
   GND ─────────────────────── common GND (all modules)
```

**Rules that are not optional:**

0. **If you have to buy one part, buy this:** a **5 V 5 A DC-DC buck**
   (Mini560, XL4015, or a 5 V 3 A UBEC rated for 2–6S input), plus a
   **3S BMS** and a **12.6 V charger**. An LM2596 works for testing but is
   only ~2 A continuous and will sag with four motors under load.

1. **Set the buck to 5.0 V with a multimeter before connecting anything.**
   Above ~5.5 V you will destroy the Uno.
2. Use a **step-down (buck)** converter rated **≥3 A**. A **5 V 3 A UBEC**
   (2–6S input) is ideal for a 3S pack. A "boost" is the wrong part here.
3. **Never connect the 3S pack to the Uno's 5 V pin.** It may go on VIN
   (7–12 V) in an emergency, but the onboard regulator will overheat with the
   servo and audio; use the buck.
4. **Remove the shield's PWR jumper** and run the 5 V rail to `EXT_PWR +`.
   This keeps motor current out of the Uno's traces.
   *Simpler alternative:* keep the jumper fitted and feed **only** `EXT_PWR`;
   the Uno then gets 5 V through the shield. **Never feed the 5 V pin and
   EXT_PWR from two different supplies.**
5. **Feed the motors from 5 V, not from the raw 11.1 V pack.** The L293D drops
   ~1.4 V, so 5 V gives the motors ~3.6 V — correct for 3–6 V TT motors.
   Raw 11.1 V would give ~9 V and burn them out.
6. **Do not use the TP4056 on this pack.** It is a **1S** charger. Use a
   **3S BMS** and a **12.6 V** charger.
7. **Never connect USB and the battery/buck at the same time** while the buck
   feeds the 5 V pin. For uploading: switch the battery off, unplug the buck
   output, then plug in USB.
8. Put **1000 µF electrolytic + 100 nF ceramic** across the 5 V rail near
   the shield. Motor starts cause huge current spikes.
9. Add **100 nF ceramic** across every motor's two terminals. This kills most
   of the RF noise that corrupts the ultrasonic reading and Bluetooth.
10. Fit a **3 A fuse** and a real **main switch** in series with the pack.
11. **All grounds must meet at one point**: Uno, shield, buck, BMS, servo,
    PAM8403, HC-05, HC-SR04. A missing ground looks exactly like a dead board.

### 3.2 Battery and 3S safety

* This is a **3S** pack. Use a **3S BMS** (or a protected 3S pack) and a
  **12.6 V CCCV charger**. **Never charge it with a TP4056** — that is a
  1-cell charger and will not stop at 12.6 V.
* Charge with the robot **switched off** and the pack **disconnected** from
  the robot if your charger is external.
* Keep all three cells at the **same voltage**; a 3S BMS balances them, but a
  badly mismatched pack is still a fire risk.
* Do not leave a charging pack unattended.
* The Buck's input must be rated for at least **16 V** (a fully charged 3S
  pack is 12.6 V).

### 3.3 Performance with a 5 V rail

With a 5 V rail the L293D drops ~1.4 V, so the motors see about **3.6 V**.
That is exactly the rating of common yellow TT gear motors, so the robot is
fast enough and the motors stay cool.

If you want more speed later, the clean upgrade is a **5 V buck** from a
**2S** pack: the motors would see ~6 V. Do not feed the raw 3S pack (~9 V
after the L293D) to 3–6 V motors.

The speed constants are PWM values (0–255), so they work with any pack.

### 3.4 No buck yet? Safe bench test with any 5 V 2 A supply

You can test the whole robot **today** with a phone charger or USB power bank
rated **5 V / 2 A**. It is not as strong as a proper 5 V 3 A buck, but it powers
the Uno, shield, servo, sensor and audio correctly.

```
  5 V 2 A charger / power bank
        |
        +---- USB  -> Uno USB socket        (logic)
        |
        +---- 5 V -> shield EXT_PWR +       (motors)
        |            PWR jumper REMOVED
        |
        +---- GND -> Uno GND and EXT_PWR GND   (common ground!)
```

Rules:

1. **Remove the shield's PWR jumper.** Otherwise the Uno's USB 5 V and the
   charger 5 V are tied together.
2. **One supply only.** Do not add the battery at the same time.
3. Common ground between the Uno and `EXT_PWR−` is required.
4. The motor test will work with the wheels off the ground. Driving on the
   floor may brown out a weak charger — that is expected without a buck.
5. This is a **bench test**, not the final power system. For the battery,
   build the 5 V buck in §3.1.

### 3.5 Finding `EXT_PWR` on the HW-130 shield (from your photo)

Your shield is an **HW-130 / L293D shield**. The power parts are:

* the **2-pin blue screw terminal** on the right-hand side, next to the
  **yellow jumper** — this is `EXT_PWR`;
* the **yellow 3-pin jumper** — this is the `PWR` jumper.

How to use it:

1. **Pull the yellow jumper off** (keep it safe). With it fitted, the motors
   run from the Uno's own 5 V, i.e. USB — that is the fault you are seeing.
2. **Find which screw is GND — do not guess.** Multimeter on continuity/ohms:
   touch one probe to the Uno's GND and each `EXT_PWR` screw in turn. The
   screw that beeps / reads ~0 Ω is **GND**; the other is **+M**. The PCB
   prints `+M` and `GND` beside the terminal as well.
3. Feed **regulated 5.0 V** to `+M` and **GND** to `GND`.
4. Run a wire from the Uno's **GND** to the same `EXT_PWR` GND (common ground).
5. The shield's green power LED must stay lit when all four motors start.

> **Never feed `EXT_PWR` while the yellow `PWR` jumper is fitted.** That ties
> your external supply straight to the Uno's 5 V rail and can destroy both
> boards. Remove it first, every time.

> **If you are not sure about the polarity, stop and measure.** Reversing
> `EXT_PWR` can kill the L293D. There is no protection diode on these clones.


---

## 4. Servo power warning
An SG90 can pull 700 mA when it starts moving. The Uno's 5 V rail (from USB)
cannot supply that plus four motors. Symptoms: the robot resets or the
Bluetooth drops when the servo moves.

→ Power the servo from the same **5 V buck rail**, with the
1000 µF capacitor nearby. All grounds must be common.

---

## 5. Uploading the sketch (important because BT is on pins 0/1)

With the HC-05 connected to pins 0 and 1, the module's TX fights the USB
adapter during uploads. Do this:

1. Unplug the HC-05 **TX and RX** wires (or unplug the module).
2. Upload.
3. Power off, reconnect the module, power on.

If you get tired of that, change one line in the sketch:

```c
#define BT_USE_SOFTWARE_SERIAL   0   ->   1
```
then move the module to `TX→A4`, `RX→A5` (divider still needed). Uploads then
always work and `Serial` is free for debugging. The firmware is compiled and
tested in both configurations.

> **Never open the Serial Monitor while the HC-05 is on pins 0/1.**
> The USB adapter and the module would both drive pin 0 at the same time —
> two outputs fighting each other. Unplug the module first.
>
> Also note the crossover: the module's **TX** goes to the Uno's **RX (pin 0)**
> and the module's **RX** goes to the Uno's **TX (pin 1)**. Connecting TX→TX
> is the most common "Bluetooth does nothing" mistake.

---

## 6. Assembly order
1. Motors + wheels on the chassis, check they spin freely.
2. **Build and measure the 5 V rail first** (§3): 3S pack + BMS → switch →
   fuse → 5 V 3 A buck. Set it to 5.0 V with no load. Do not connect the Uno
   yet.
3. Shield on the Uno, motors into M1–M4.
4. Servo + HC-SR04 on the front.
5. **Button on A3 and GND**, Bluetooth, amplifier.
6. Power off. Multimeter on continuity: **check 5 V and GND are not shorted.**
7. Connect the 5 V rail to the Uno and the shield, then run `TEST_PLAN.md`.
