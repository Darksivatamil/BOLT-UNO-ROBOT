# Upload — BOLT UNO ROBOT v2.0

The firmware is **already compiled**. You only need to flash it.

> **Before you upload, check the power.** Your pack is **3S (~11–12 V)**.
> Put a **5 V 3 A buck/UBEC** between the pack and the Uno **5 V pin**.
> Never connect the 3S pack directly to the 5 V pin, and never charge it with
> a TP4056 (1S only). See `WIRING.md` §3.

## Build artifact (verified)

```
BOLT_UNO_ROBOT/build/arduino.avr.uno/BOLT_UNO_ROBOT.ino.hex

Program:   11108 bytes  (34.4% of 32 KB flash)
Data:        716 bytes  (35.0% of 2 KB RAM)
sha256:    334e5953003185cee049298d1a5c8e24a13e64a79ba1f9805e4ec639fd8ff121
```

---

## Before you click upload (30 seconds, saves an hour)

| Check | Why |
|-------|-----|
| **HC-05/06 TX and RX wires unplugged** | They sit on pins 0/1 and fight the USB adapter. Upload fails with `stk500_recv()`. |
| **Battery switched off / buck output unplugged** | USB and the 5 V buck must never feed the board at the same time. |
| Board = **Arduino Uno** | Wrong FQBN = wrong bootloader settings |
| USB cable is a **data** cable | Charge-only cables show no port |
| You are in the `dialout` group | Otherwise the port is "permission denied" |

---

## Option A — Arduino IDE (easiest)

1. Open `BOLT_UNO_ROBOT/BOLT_UNO_ROBOT.ino`.
2. **Tools → Board → Arduino AVR Boards → Arduino Uno**
3. **Tools → Port →** the port that appears when you plug the Uno in.
4. Click **Upload** (→ arrow).
5. When it says *Done uploading*, switch off, reconnect the HC-05 TX/RX,
   switch on.

> If `Servo.h: No such file or directory`: **Sketch → Include Library →
> Manage Libraries → search "Servo" → Install**.

---

## Option B — command line (one command)

```bash
cd "BOLT UNO ROBOT"            # the folder containing upload.sh
./upload.sh                    # auto-detects the port, compiles, uploads, verifies
```

Or explicitly:

```bash
./upload.sh /dev/ttyACM0
```

Compile only (no board attached):

```bash
./upload.sh --build
```

Build and flash the **hardware diagnostic** instead:

```bash
./upload.sh --test
```

Manual equivalent:

```bash
arduino-cli compile --fqbn arduino:avr:uno BOLT_UNO_ROBOT
arduino-cli upload  --fqbn arduino:avr:uno -p /dev/ttyACM0 --verify BOLT_UNO_ROBOT
```

---

## Option C — flash the prebuilt .hex directly

If you have no toolchain at all, `avrdude` can flash the existing file:

```bash
avrdude -c arduino -p atmega328p -P /dev/ttyACM0 -b 115200 \
        -U flash:w:BOLT_UNO_ROBOT/build/arduino.avr.uno/BOLT_UNO_ROBOT.ino.hex:i
```

---

## Right after the upload

1. Open **Serial Monitor at 9600** (with the HC-05 still unplugged).
   You should see:

   ```
   BOLT v2.0 READY
   BTN:A3-GND  PIN8=motor data, never a button
   ```

   No text means the board is not running — go back to the power check.

2. **Reconnect** the Bluetooth module (TX→pin 0, RX→pin 1 through the divider).
3. **Hold the button**, then power on → hardware self test.
   M1, M2, M3, M4 should each kick forward once, then both sides forward/back,
   then the servo sweeps. This proves every wire.
4. Power off, power on normally → boot melody + 3 s sweep → obstacle mode.

If a wheel runs backwards, set the matching `INV_Mx` to `1` and re-upload.

If nothing moves but the LED blinks and you hear the 2.4 s low warning, the
HC-SR04 is not answering — upload `BOLT_TEST` and follow `TEST_PLAN.md`
Phase 2.

Full sequence with pass/fail criteria: `TEST_PLAN.md`.
