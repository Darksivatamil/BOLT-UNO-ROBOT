/* ============================================================================
 *  BOLT UNO ROBOT - HARDWARE TEST  (upload this FIRST)
 *  ---------------------------------------------------------------------------
 *  It tests every part separately and prints the result at 9600 baud.
 *  Nothing here depends on the main firmware.
 *
 *  Open Serial Monitor at 9600 and watch the checklist.
 *
 *  BUTTON ON A3: leg 1 -> A3, leg 2 -> GND. Uses INPUT_PULLUP, no resistor.
 *  (Pin 8 cannot be used: the L293D shield holds it low. Pin 2 is usually
 *  hidden under the stacked shield, so A3 is the practical choice.)
 * ==========================================================================*/

#include <Servo.h>

/* ---- pins (same as the main firmware) ---- */
#define PIN_SERVO   10
#define PIN_TRIG    A0
#define PIN_ECHO    A1
#define PIN_AUDIO   A2
#define PIN_LED     13
#define PIN_BUTTON  A3

#define SR_LATCH    12
#define SR_CLK      4
#define SR_ENABLE   7
#define SR_DATA     8
#define PWM_M1      11
#define PWM_M2      3
#define PWM_M3      6
#define PWM_M4      5

#define BTN_MULTIPLEXED  (PIN_BUTTON == SR_DATA)

#define BIT_M1A 2
#define BIT_M1B 3
#define BIT_M2A 1
#define BIT_M2B 4
#define BIT_M3A 5
#define BIT_M3B 7
#define BIT_M4A 0
#define BIT_M4B 6

Servo servo;

/* ===========================================================================
 *  software tone (no Timer2 conflict with M1/M2 PWM)
 * ==========================================================================*/
static uint16_t halfUs = 0;
static uint32_t nextEdge = 0;
static bool     level = false;
static uint32_t toneEnd = 0;

static void toneStart(uint16_t hz, uint16_t ms) {
  if (hz < 30) { halfUs = 0; digitalWrite(PIN_AUDIO, LOW); return; }
  halfUs = (uint16_t)(500000UL / hz);
  nextEdge = micros();
  level = false;
  toneEnd = millis() + ms;
}

static void toneUpdate() {
  if (!halfUs) return;
  uint32_t now = micros();
  if ((int32_t)(now - nextEdge) >= 0) {
    level = !level;
    digitalWrite(PIN_AUDIO, level ? HIGH : LOW);
    nextEdge += halfUs;
    if ((int32_t)(now - nextEdge) > 2000) nextEdge = now + halfUs;
  }
  if ((int32_t)(millis() - toneEnd) >= 0) {
    halfUs = 0;
    digitalWrite(PIN_AUDIO, LOW);
  }
}

static void beep(uint16_t hz, uint16_t ms) {
  toneStart(hz, ms);
  uint32_t t0 = millis();
  while ((uint32_t)(millis() - t0) < ms) { toneUpdate(); }
  halfUs = 0;
  digitalWrite(PIN_AUDIO, LOW);
}

/* ===========================================================================
 *  motors
 * ==========================================================================*/
static uint8_t sr = 0;
static bool    buttonStuck = false;   // pin 8 low even with no press

/* read the button without fighting the 595 (see the main firmware) */
static bool readBtn() {
#if BTN_MULTIPLEXED
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  delayMicroseconds(10);
  bool pressed = (digitalRead(PIN_BUTTON) == LOW);
  digitalWrite(PIN_BUTTON, LOW);      // pullup off before driving
  pinMode(PIN_BUTTON, OUTPUT);
  return pressed;
#else
  return (digitalRead(PIN_BUTTON) == LOW);
#endif
}

static void srShiftOut() {
  digitalWrite(SR_LATCH, LOW);
  for (int8_t i = 7; i >= 0; --i) {
    digitalWrite(SR_CLK, LOW);
    digitalWrite(SR_DATA, (sr >> i) & 1);
    digitalWrite(SR_CLK, HIGH);
  }
  digitalWrite(SR_LATCH, HIGH);
  digitalWrite(SR_DATA, LOW);          // idle low: a pin-8 button cannot short a HIGH
}

static void srWrite() {
#if BTN_MULTIPLEXED
  if (readBtn()) {                     // never shift a HIGH into a held button
    uint8_t saved = sr;
    sr = 0;
    srShiftOut();
    sr = saved;
    return;
  }
#endif
  srShiftOut();
}

static void wheel(uint8_t a, uint8_t b, uint8_t pwm, int16_t spd) {
  if (spd > 255) spd = 255;
  if (spd < -255) spd = -255;
  if (spd > 0)      { sr |= (1 << a); sr &= ~(1 << b); analogWrite(pwm, spd); }
  else if (spd < 0) { sr &= ~(1 << a); sr |= (1 << b); analogWrite(pwm, -spd); }
  else              { sr &= ~(1 << a); sr &= ~(1 << b); analogWrite(pwm, 0); }
}

static void allStop() {
  sr = 0;
  analogWrite(PWM_M1, 0); analogWrite(PWM_M2, 0);
  analogWrite(PWM_M3, 0); analogWrite(PWM_M4, 0);
  srWrite();
}

static void motorTest(uint8_t idx, int16_t spd) {
  allStop();
  switch (idx) {
    case 0: wheel(BIT_M1A, BIT_M1B, PWM_M1, spd); break;
    case 1: wheel(BIT_M2A, BIT_M2B, PWM_M2, spd); break;
    case 2: wheel(BIT_M3A, BIT_M3B, PWM_M3, spd); break;
    case 3: wheel(BIT_M4A, BIT_M4B, PWM_M4, spd); break;
  }
  srWrite();
}

/* ===========================================================================
 *  ultrasonic
 * ==========================================================================*/
/* Returns:
 *   0   -> sensor did not answer (dead / unpowered / wrong pin)
 *   999 -> sensor answered, nothing within ~4 m
 *   n   -> distance in cm
 * A working HC-SR04 holds ECHO high ~38 ms when it hears no echo, so the two
 * "no distance" cases are distinguishable.
 */
static uint16_t readCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(3);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  uint32_t t0 = micros();
  while (digitalRead(PIN_ECHO) == LOW) {
    if ((uint32_t)(micros() - t0) > 25000UL) return 0;   // never went high = dead
  }
  uint32_t t1 = micros();
  while (digitalRead(PIN_ECHO) == HIGH) {
    if ((uint32_t)(micros() - t1) > 25000UL) {
      uint32_t t2 = micros();                            // let the 38 ms pulse end
      while (digitalRead(PIN_ECHO) == HIGH) {
        if ((uint32_t)(micros() - t2) > 40000UL) break;
      }
      return 999;                                        // alive, nothing in range
    }
  }
  uint32_t d = micros() - t1;
  if (d < 60) return 999;
  return (uint16_t)(d / 58UL);
}

/* ===========================================================================
 *  setup / test sequence
 * ==========================================================================*/
void setup() {
  Serial.begin(9600);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_AUDIO, OUTPUT);
  digitalWrite(PIN_AUDIO, LOW);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  pinMode(PIN_BUTTON, INPUT_PULLUP);

  pinMode(SR_LATCH, OUTPUT);
  pinMode(SR_CLK, OUTPUT);
  pinMode(SR_ENABLE, OUTPUT);
  pinMode(SR_DATA, OUTPUT);
  pinMode(PWM_M1, OUTPUT);
  pinMode(PWM_M2, OUTPUT);
  pinMode(PWM_M3, OUTPUT);
  pinMode(PWM_M4, OUTPUT);
  digitalWrite(SR_ENABLE, LOW);
  allStop();

  delay(1200);   // USB settle + let the monitor attach

  Serial.println();
  Serial.println(F("=========================================="));
  Serial.println(F("  BOLT UNO ROBOT  -  HARDWARE TEST"));
  Serial.println(F("=========================================="));

  /* ---------- 0. button pin check ---------- */
#if BTN_MULTIPLEXED
  Serial.println(F("BUTTON: pin 8 (motor data line) through 1k to GND."));
  if (!readBtn()) {
    Serial.println(F("        pin 8 reads HIGH when released - wiring looks right."));
  } else {
    Serial.println(F("        !! PIN 8 READS LOW WITH NO PRESS !!"));
    Serial.print(F("        release the button / check the 1k"));
    uint32_t wr = millis();
    bool released = false;
    while ((uint32_t)(millis() - wr) < 6000UL) {
      toneUpdate();
      if (!readBtn()) { released = true; break; }
      delay(20);
    }
    Serial.println(released ? F("  -> released, OK") : F("  -> STILL LOW"));
    if (!released) {
      buttonStuck = true;
      Serial.println(F("        You are not touching it, so pin 8 is shorted:"));
      Serial.println(F("          * switch wired across two SAME-SIDE legs (always on), or"));
      Serial.println(F("          * the shield holds the data line low (then pin 8 cannot"));
      Serial.println(F("            be used as a button - use pin 2)."));
      Serial.println(F("        To test the motors now: unplug the pin-8 button wire and"));
      Serial.println(F("        press RESET. Motor test will run with the line free."));
    }
  }
#else
  Serial.println(F("BUTTON: A3 to GND (INPUT_PULLUP, no resistor)."));
  if (readBtn()) {
    Serial.print(F("        A3 is LOW - release the button"));
    uint32_t wr = millis();
    while (readBtn() && (uint32_t)(millis() - wr) < 5000UL) { toneUpdate(); delay(20); }
    Serial.println(readBtn() ? F("  -> STILL LOW (check wiring)") : F("  -> released, OK"));
  } else {
    Serial.println(F("        A3 reads HIGH (released) - wiring looks right."));
  }
#endif

  /* ---------- 1. LED ---------- */
  Serial.println(F("[1/6] LED ................ board is running"));
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(PIN_LED, HIGH); delay(150);
    digitalWrite(PIN_LED, LOW);  delay(150);
  }

  /* ---------- 2. AUDIO ---------- */
  Serial.println(F("[2/6] AUDIO .............. 3 rising tones"));
  beep(523, 150); beep(784, 150); beep(1047, 250);

  /* ---------- 3. SERVO ---------- */
  Serial.println(F("[3/6] SERVO .............. 0 -> 90 -> 180 -> 90"));
  servo.attach(PIN_SERVO);
  servo.write(0);   delay(600);
  servo.write(90);  delay(600);
  servo.write(180); delay(600);
  servo.write(90);  delay(400);
  Serial.println(F("      servo done"));

  /* ---------- 4. ULTRASONIC ---------- */
  /* The HC-SR04 needs a moment after power-up and after the servo moved,
   * otherwise the first pings falsely look like a dead sensor. */
  delay(250);
  for (uint8_t i = 0; i < 3; i++) { (void)readCm(); delay(70); }

  Serial.print(F("[4/6] ULTRASONIC ......... "));
  uint8_t answered = 0;
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t d = readCm();
    if (d == 0) {
      Serial.print(F("--  "));
    } else {
      answered++;
      if (d >= 999) { Serial.print(F(">400 ")); }
      else          { Serial.print(d); Serial.print(F("cm ")); }
    }
    delay(70);
  }
  Serial.println();
  if (answered == 0) {
    Serial.println(F("      FAIL: sensor never answered. Check A0/A1, 5V, GND."));
  } else if (answered < 5) {
    Serial.println(F("      WEAK: answered only sometimes. Check 5V/GND + wiring."));
  } else {
    Serial.println(F("      OK"));
  }

  /* ---------- 5. MOTORS ---------- */
  Serial.println(F("[5/6] MOTORS ............. M1 M2 M3 M4, low speed 0.5s each"));
  if (buttonStuck) {
    Serial.println(F("      SKIPPED: pin 8 is stuck low. The motor data is forced"));
    Serial.println(F("      to zero to protect the pin. Unplug the button wire and"));
    Serial.println(F("      RESET to run this test."));
  } else {
    const char* names[4] = {"M1 (left front)", "M2 (left rear)",
                            "M3 (right front)", "M4 (right rear)"};
    for (uint8_t i = 0; i < 4; i++) {
      Serial.print(F("      ")); Serial.print(names[i]);
      Serial.println(F("  FORWARD"));
      motorTest(i, 110);
      delay(500);
      allStop();
      delay(250);
    }
    Serial.println(F("      all four forward"));
    allStop();
    wheel(BIT_M1A, BIT_M1B, PWM_M1, 110);
    wheel(BIT_M2A, BIT_M2B, PWM_M2, 110);
    wheel(BIT_M3A, BIT_M3B, PWM_M3, 110);
    wheel(BIT_M4A, BIT_M4B, PWM_M4, 110);
    srWrite();
    delay(900);
    allStop();
    Serial.println(F("      motors done"));
  }

  /* ---------- 6. BUTTON ---------- */
  if (buttonStuck) {
    Serial.println(F("[6/6] BUTTON ............. already reads LOW with no press"));
    Serial.println(F("      FAIL: fix the switch wiring, then re-run this test."));
  } else {
    Serial.println(F("[6/6] BUTTON ............. press it within 10 s"));
    uint32_t t0 = millis();
    bool last = false, seen = false;
    uint32_t lastEdge = 0;
    while ((uint32_t)(millis() - t0) < 10000UL) {
      bool now = readBtn();
      if (now != last) {
        last = now;
        lastEdge = millis();
      } else if ((uint32_t)(millis() - lastEdge) >= 40UL) {
        /* stable for 40 ms: report once */
        static int8_t reported = -1;
        int8_t v = now ? 1 : 0;
        if (v != reported) {
          reported = v;
          Serial.print(F("      button "));
          Serial.println(now ? F("PRESSED") : F("released"));
          if (now) seen = true;
        }
      }
      toneUpdate();
      delay(10);
    }
    Serial.println(seen ? F("      button OK") : F("      button NEVER PRESSED - check the button wiring"));
  }

  Serial.println(F("=========================================="));
  Serial.println(F("  LIVE STATUS"));
  Serial.println(F("  dist | button"));
  Serial.println(F("=========================================="));
}

/* ===========================================================================
 *  live status forever
 * ==========================================================================*/
void loop() {
  static uint32_t last = 0;
  static uint32_t lastBeep = 0;

  if ((uint32_t)(millis() - last) >= 500) {
    last = millis();

    uint16_t d = readCm();
    bool btn = readBtn();

    Serial.print(F("dist="));
    if (d == 0)        { Serial.print(F("--")); }
    else if (d >= 999) { Serial.print(F(">400cm")); }
    else               { Serial.print(d); Serial.print(F("cm")); }
    Serial.print(F("  button=")); Serial.print(btn ? F("DOWN") : F("up"));
    if (buttonStuck) Serial.print(F("  (pin8 stuck low!)"));
    Serial.println();
  }

  /* keep the LED alive so you know the board has not frozen */
  if ((uint32_t)(millis() - lastBeep) >= 1000) {
    lastBeep = millis();
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }

  toneUpdate();
}
