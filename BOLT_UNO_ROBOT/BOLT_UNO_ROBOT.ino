/* ============================================================================
 *  BOLT UNO ROBOT  -  firmware v2.0
 *  ---------------------------------------------------------------------------
 *  3 modes, cycled with one button:
 *      1) OBSTACLE  - fast, deterministic "robot" behaviour
 *      2) BLUETOOTH - manual control from any BT RC app (tested protocol:
 *                     "Bluetooth RC Car" / "Bluetooth RC controller")
 *      3) AI        - slower, smoother, 5-zone "smart" behaviour
 *
 *  Hardware : Arduino Uno + L293D motor shield (HW-130) + HC-SR04 +
 *             SG90 on SER1 + HC-05/06 + PAM8403 + 5V speaker + 1 button
 *
 *  READ THESE FILES FIRST:
 *      WIRING.md      - exact connections + power warnings
 *      TEST_PLAN.md   - step-by-step bring-up (do NOT skip this)
 *
 *  Libraries : Servo (bundled with the Arduino IDE). Nothing else.
 *
 *  ---------------------------------------------------------------------------
 *  v2.0 changes (why this file exists):
 *    * The robot NEVER drives blind. If the HC-SR04 stops answering it stops
 *      and beeps instead of treating "no echo" as "clear road".
 *    * Button: short press = next mode, hold 1.4 s = emergency stop / resume.
 *    * Bluetooth accepts single characters AND whole words ("forward", ...).
 *    * Compile-time #error if the button/audio is put on a motor-shield pin.
 *    * The median filter no longer lets one timeout hide a real distance.
 * ==========================================================================*/

#include <Servo.h>

/* ===========================================================================
 *  SECTION 1 - USER CONFIGURATION
 * ==========================================================================*/

/* ---- Bluetooth -----------------------------------------------------------
 * 0 = HC-05/06 on hardware serial, pin 0 (RX) and pin 1 (TX).
 *     !! You MUST disconnect the BT module's RX/TX wires while uploading,
 *        otherwise the upload fails. Also HC-05 RX needs a 1k/2k divider.
 * 1 = HC-05/06 on SoftwareSerial, A4 (RX) and A5 (TX). Uploads always work,
 *     and Serial stays free for debugging. Recommended if you hit trouble.
 */
#define BT_USE_SOFTWARE_SERIAL   0
#define BT_RX_PIN                A4     // only used when the line above is 1
#define BT_TX_PIN                A5
#define BT_BAUD                  9600   // HC-05/06 factory default

/* Print debug text on the hardware serial port.
 * Leave 0 when Bluetooth is on hardware serial (they would fight).
 * Set 1 only when BT_USE_SOFTWARE_SERIAL is 1 (or BT is unplugged). */
#define ENABLE_DEBUG             0

#if ENABLE_DEBUG && !BT_USE_SOFTWARE_SERIAL
  #error "ENABLE_DEBUG needs BT_USE_SOFTWARE_SERIAL 1: on pins 0/1 Serial is the Bluetooth link."
#endif

/* ---- Pin map -------------------------------------------------------------
 *  Pins 3,4,5,6,7,8,11,12 are hard-wired to the L293D shield. Do not move.
 *  Pin 6 CANNOT be the button: it is M3's PWM line on this shield.
 *
 *  BUTTON ON A3.
 *  Pin 8 was tried but the L293D shield holds that line low permanently
 *  (74HC595 SER input / pull-down), so a button there can never read HIGH.
 *  Pin 2 is usually hidden under the stacked shield, so the button lives on
 *  A3 (exposed on the analog header, free: A0/A1 = sensor, A2 = audio).
 *  A3 uses INPUT_PULLUP and needs no resistor.
 *
 *  The pin-8 multiplexing code below is kept (BTN_MULTIPLEXED) so the sketch
 *  still builds safely if someone sets PIN_BUTTON back to 8, but on this
 *  shield that pin reads LOW and is not usable as a button.
 */
#define PIN_SERVO                10     // SER1 header on the shield
#define PIN_TRIG                 A0
#define PIN_ECHO                 A1
#define PIN_BUTTON               A3     // other leg -> GND, INPUT_PULLUP
#define PIN_AUDIO                A2     // -> 1k -> PAM8403 L input
#define PIN_LED                  13     // on-board LED, mode indicator

/* ---- Motor shield (fixed hardware, do not edit) -------------------------- */
#define SR_LATCH                 12
#define SR_CLK                   4
#define SR_ENABLE                7
#define SR_DATA                  8

#define PWM_M1                   11
#define PWM_M2                   3
#define PWM_M3                   6
#define PWM_M4                   5

/* 74HC595 output bit for each H-bridge input */
#define BIT_M1A                  2
#define BIT_M1B                  3
#define BIT_M2A                  1
#define BIT_M2B                  4
#define BIT_M3A                  5
#define BIT_M3B                  7
#define BIT_M4A                  0
#define BIT_M4B                  6

/* ---- Compile-time pin safety checks --------------------------------------
 * Putting the button (or the audio output) on a motor-shield pin is not a
 * small mistake: the button shorts an Arduino OUTPUT to GND and corrupts the
 * 74HC595 data stream. The build refuses to continue instead of letting that
 * happen silently.
 */
#if (PIN_BUTTON == SR_LATCH) || (PIN_BUTTON == SR_CLK) || (PIN_BUTTON == SR_ENABLE) || (PIN_BUTTON == PWM_M1) || (PIN_BUTTON == PWM_M2) || (PIN_BUTTON == PWM_M3) || (PIN_BUTTON == PWM_M4)
  #error "PIN_BUTTON is on a motor-shield pin. Use A3 (or pin 2), never a motor pin."
#endif

/* Pin 8 is supported, but only with the multiplexing below and a 1k series
 * resistor. The message is a warning, not an error, because you chose it. */
#if (PIN_BUTTON == SR_DATA)
  #warning "Button on pin 8 (74HC595 DATA). A 1k resistor in series to GND is REQUIRED."
  #define BTN_MULTIPLEXED 1
#else
  #define BTN_MULTIPLEXED 0
#endif

#if (PIN_AUDIO == SR_LATCH) || (PIN_AUDIO == SR_CLK) || (PIN_AUDIO == SR_ENABLE) || (PIN_AUDIO == SR_DATA) || (PIN_AUDIO == PWM_M1) || (PIN_AUDIO == PWM_M2) || (PIN_AUDIO == PWM_M3) || (PIN_AUDIO == PWM_M4)
  #error "PIN_AUDIO is on a motor-shield pin. Use A2 through 1k to the PAM8403."
#endif

#if (PIN_TRIG == SR_DATA) || (PIN_ECHO == SR_DATA) || (PIN_TRIG == SR_LATCH) || (PIN_ECHO == SR_LATCH)
  #error "An ultrasonic pin is on a motor-shield pin. Use A0 (TRIG) and A1 (ECHO)."
#endif

#if (PIN_SERVO != 9) && (PIN_SERVO != 10)
  #error "PIN_SERVO must be 9 or 10 (the shield's SER1/SER2 header)."
#endif

/* Two functions must never share one pin. static_assert is used here because
 * A0/A1/A2 are const variables, not preprocessor numbers. */
static_assert(PIN_BUTTON != PIN_AUDIO, "PIN_BUTTON and PIN_AUDIO share a pin.");
static_assert(PIN_BUTTON != PIN_SERVO, "PIN_BUTTON and PIN_SERVO share a pin.");
static_assert(PIN_BUTTON != PIN_TRIG,  "PIN_BUTTON and PIN_TRIG share a pin.");
static_assert(PIN_BUTTON != PIN_ECHO,  "PIN_BUTTON and PIN_ECHO share a pin.");
static_assert(PIN_TRIG   != PIN_ECHO,  "TRIG and ECHO share a pin.");
static_assert(PIN_TRIG   != PIN_AUDIO, "TRIG and PIN_AUDIO share a pin.");
static_assert(PIN_ECHO   != PIN_AUDIO, "ECHO and PIN_AUDIO share a pin.");
static_assert(PIN_AUDIO  != PIN_SERVO, "PIN_AUDIO and PIN_SERVO share a pin.");
static_assert(PIN_SERVO  != PIN_TRIG,  "PIN_SERVO and PIN_TRIG share a pin.");
static_assert(PIN_SERVO  != PIN_ECHO,  "PIN_SERVO and PIN_ECHO share a pin.");
static_assert(PIN_BUTTON != 0 && PIN_BUTTON != 1,
              "Pins 0/1 are the Bluetooth serial port. Use pin 2 for the button.");

/* M1+M2 = LEFT side, M3+M4 = RIGHT side.
 * If one wheel spins the wrong way, change its 0 to 1. */
#define INV_M1                   0
#define INV_M2                   0
#define INV_M3                   0
#define INV_M4                   0

/* ---- Geometry ------------------------------------------------------------
 * Servo angle that points the sensor straight ahead.
 * If the servo sweeps the wrong way (looks "right" when it should look
 * "left") just swap SIDE_RIGHT_ANGLE and SIDE_LEFT_ANGLE. */
#define ANGLE_CENTER             90
#define SIDE_RIGHT_ANGLE         0
#define SIDE_LEFT_ANGLE          180
#define SCAN_45_RIGHT            45
#define SCAN_45_LEFT             135

/* ---- Obstacle mode tuning ------------------------------------------------ */
#define OBSTACLE_STOP_CM         15     // your spec: stop at 15 cm
#define OBSTACLE_RESUME_CM       22     // must be this clear before driving on
#define OBSTACLE_SLOW_CM         45     // start easing off (braking distance)
#define OBSTACLE_SPEED           190    // 0..255  (fast, "robot" style)
#define OBSTACLE_SLOW_SPEED      115    // approach speed
#define OBSTACLE_TURN_SPEED      175
#define OBSTACLE_BACK_SPEED      170
#define TURN90_MS                520    // <-- calibrate (TEST_PLAN step 6)
#define TURN180_MS               (TURN90_MS * 2)
#define REVERSE_MS               700    // reverse escape duration

/* ---- Bluetooth mode tuning ---------------------------------------------- */
#define BT_MAX_SPEED             255    // spec: bluetooth mode is faster
#define BT_MIN_SPEED             110
#define BT_FAILSAFE_MS           1200   // no command for this long -> stop
#define BT_COLLISION_GUARD       0      // 1 = auto stop below 10 cm
#define BT_GUARD_CM              10

/* ---- AI mode tuning ------------------------------------------------------ */
#define AI_CRUISE_SPEED          135    // spec: slower than obstacle mode
#define AI_MIN_SPEED             95
#define AI_TURN_SPEED            115
#define AI_BACK_SPEED            120
#define AI_SLOW_DIST_CM          60     // start easing off
#define AI_STOP_DIST_CM          18
#define AI_CLEAR_MIN_CM          26     // a side gap must be this big to use
#define AI_STEER_GAIN            3      // differential steer strength
#define AI_BACKUP_MS             650
#define AI_SAMPLE_MS             70

/* ---- Motion ramps -------------------------------------------------------- */
#define RAMP_PERIOD_MS           20
#define RAMP_STEP_OBSTACLE       30
#define RAMP_STEP_AI             7      // smooth, human-like
#define RAMP_STEP_BT             255    // instant, manual control

/* ---- Misc ---------------------------------------------------------------- */
#define BOOT_TIME_MS             3000UL // loading animation duration
#define MODE_CHANGE_CHIRP        1      // short sound when the mode changes
                                        // (set 0 for totally silent BT mode)
#define ECHO_TIMEOUT_US          25000UL
#define DIST_NO_ECHO             999    // sensor answered, nothing within ~4 m
#define DIST_DEAD                0      // sensor did NOT answer at all (fault)
#define DIST_MAX_CM              400

/* ---- Button timings (v2.0) ----------------------------------------------- */
#define BTN_DEBOUNCE_MS          25
#define BTN_SHORT_MAX_MS         900    // release before this = next mode
#define BTN_LONG_MS              1400   // held this long = stop / resume

/* ---- Ultrasonic health (v2.0) --------------------------------------------
 * "No echo" is NOT "road is clear". If the sensor never answers, the robot
 * waits, then stops and beeps. It never drives blind.
 */
#define SENSOR_BOOT_WAIT_MS      2200UL // time allowed for the first echo
#define SENSOR_GRACE_MS          600UL  // tolerate a short dropout after that

/* 0 = SAFE (default): no echo means "I cannot see", so stop and re-ping.
 * 1 = only for a very large room: no echo is treated as clear road, and the
 *     robot creeps forward at the slow speed. Never use 1 if the sensor could
 *     come loose - that is exactly the "drives into the wall" failure.
 */
#define NO_ECHO_MEANS_CLEAR      0

/* ===========================================================================
 *  SECTION 2 - TYPES, GLOBALS, MELODIES
 * ==========================================================================*/

enum Mode : uint8_t { MODE_OBSTACLE = 0, MODE_BLUETOOTH = 1, MODE_AI = 2 };

/* Ultrasonic health result, declared here because the Arduino preprocessor
 * inserts function prototypes above the rest of the file. */
enum SensorState : uint8_t { SENSOR_OK, SENSOR_WAIT, SENSOR_FAULT };

static Mode mode = MODE_OBSTACLE;

struct Note { uint16_t hz; uint16_t ms; };

/* Note frequencies (Hz) */
#define N_REST  0
#define N_G3    196
#define N_A3    220
#define N_B3    247
#define N_C4    262
#define N_D4    294
#define N_E4    330
#define N_F4    349
#define N_G4    392
#define N_A4    440
#define N_B4    494
#define N_C5    523
#define N_D5    587
#define N_E5    659
#define N_F5    698
#define N_G5    784
#define N_A5    880
#define N_B5    988
#define N_C6    1047
#define N_D6    1175
#define N_E6    1319
#define N_F6    1397
#define N_G6    1568
#define N_A6    1760
#define N_B6    1976
#define N_C7    2093

/* --- 8-bit style tunes ---------------------------------------------------- */

/* Power-on / loading sweep (~2.6 s) */
static const Note MEL_BOOT[] = {
  {N_C5, 120}, {N_E5, 120}, {N_G5, 120}, {N_C6, 240}, {N_REST, 60},
  {N_G5, 120}, {N_C6, 120}, {N_E6, 240}, {N_REST, 60},
  {N_C6, 100}, {N_E6, 100}, {N_G6, 320}, {N_REST, 120},
  {N_E6, 120}, {N_G6, 120}, {N_C7, 300}, {N_REST, 200}
};

/* Mode changed OBSTACLE -> BLUETOOTH */
static const Note MEL_TO_BT[] = {
  {N_G5, 70}, {N_C6, 70}, {N_E6, 120}
};

/* Mode changed BLUETOOTH -> AI */
static const Note MEL_TO_AI[] = {
  {N_C6, 60}, {N_E6, 60}, {N_G6, 60}, {N_C7, 170}
};

/* Mode changed AI -> OBSTACLE */
static const Note MEL_TO_OBSTACLE[] = {
  {N_C6, 70}, {N_G5, 70}, {N_C5, 140}
};

/* AI thinking */
static const Note MEL_AI_THINK[] = {
  {N_A5, 45}, {N_C6, 45}, {N_A5, 45}, {N_C6, 45}
};

/* AI found a clear path */
static const Note MEL_AI_CLEAR[] = {
  {N_C6, 50}, {N_G6, 70}
};

/* AI fully blocked */
static const Note MEL_AI_BLOCKED[] = {
  {N_A4, 100}, {N_F4, 140}
};

/* AI backing up */
static const Note MEL_AI_BACK[] = {
  {N_G4, 80}, {N_E4, 80}, {N_C4, 140}
};

/* Sensor fault: ~2.4 s low warning. The robot is stopped while this plays. */
static const Note MEL_SENSOR_FAULT[] = {
  {N_A3, 180}, {N_REST, 90}, {N_A3, 180}, {N_REST, 90},
  {N_A3, 180}, {N_REST, 90}, {N_G3, 400}, {N_REST, 300},
  {N_A3, 180}, {N_REST, 90}, {N_G3, 600}
};

/* Emergency stop */
static const Note MEL_HALT[] = {
  {N_G4, 120}, {N_E4, 120}, {N_C4, 300}
};

/* ------------------------------------------------------------------------- */

static Servo scanServo;
static uint8_t servoPos = ANGLE_CENTER;

#if BT_USE_SOFTWARE_SERIAL
  #include <SoftwareSerial.h>
  static SoftwareSerial btPort(BT_RX_PIN, BT_TX_PIN);
  #define BT_PORT btPort
#else
  #define BT_PORT Serial
#endif

#if ENABLE_DEBUG
  #define DBG(x)    do { Serial.print(x); } while (0)
  #define DBGLN(x)  do { Serial.println(x); } while (0)
#else
  #define DBG(x)    do { } while (0)
  #define DBGLN(x)  do { } while (0)
#endif

/* button (v2.0: short press = next mode, long press = stop/resume) */
static bool     btnLastRaw     = false;
static bool     btnStable      = false;
static uint32_t btnLastChange  = 0;
static uint32_t btnLastPoll    = 0;
static uint32_t btnPressStart  = 0;
static bool     btnLongFired   = false;
static bool     btnShortEvent  = false;   // consumed by loop()
static bool     btnLongEvent   = false;   // consumed by loop()
static bool     inputLocked    = false;   // true while boot/self test runs
static bool     halted         = false;   // emergency-stop latch

/* motor ramp */
static int16_t  curL = 0, curR = 0;
static int16_t  tgtL = 0, tgtR = 0;
static uint8_t  rampStep = RAMP_STEP_OBSTACLE;
static uint32_t rampLast = 0;
static bool     srRestore = false;   // re-send motor data after a pin-8 press

/* bluetooth */
static uint32_t btLastCmd = 0;
static uint8_t  btSpeed   = BT_MAX_SPEED;
static bool     btMoving  = false;

/* ai */
static int16_t  aiBias      = 0;
static int8_t   aiLastTurn  = 0;
static uint32_t aiLastSample = 0;
static uint16_t aiDist[5];              // -90,-45,0,+45,+90
static const uint8_t AI_ANGLES[5] = {
  SIDE_RIGHT_ANGLE, SCAN_45_RIGHT, ANGLE_CENTER, SCAN_45_LEFT, SIDE_LEFT_ANGLE
};

/* ultrasonic health (v2.0) */
static bool     sensorSeenEver  = false;
static bool     sensorFault     = false;
static uint32_t lastValidEchoMs = 0;
static uint32_t sensorBootMs    = 0;

/* ===========================================================================
 *  SECTION 3 - AUDIO ENGINE (software square wave, no timers used)
 *
 *  tone() is NOT used on purpose: on the Uno tone() seizes Timer2, and
 *  Timer2 drives the PWM outputs of M1 (pin 11) and M2 (pin 3) on this
 *  shield. Beeping would make the left wheels stutter. This engine toggles
 *  a plain GPIO with micros() and is safe to call from every tight loop.
 * ==========================================================================*/

static const Note* melPtr   = nullptr;
static uint8_t     melLen   = 0;
static uint8_t     melIdx   = 0;
static bool        melActive = false;
static uint32_t    melNoteEnd = 0;
static uint32_t    sndNextEdge = 0;
static uint16_t    sndHalfUs = 0;
static bool        sndLevel = false;

static void audioSilence() {
  sndHalfUs = 0;
  digitalWrite(PIN_AUDIO, LOW);
}

static void audioSetNote(uint16_t hz) {
  if (hz < 20) { audioSilence(); return; }
  uint16_t half = (uint16_t)(500000UL / hz);
  if (half < 15) half = 15;
  sndHalfUs    = half;
  sndNextEdge  = micros();
  sndLevel     = false;
  digitalWrite(PIN_AUDIO, LOW);
}

static void audioNextNote() {
  while (melIdx < melLen) {
    Note n = melPtr[melIdx++];
    if (n.ms == 0) continue;
    audioSetNote(n.hz);
    melNoteEnd = micros() + (uint32_t)n.ms * 1000UL;
    return;
  }
  melActive = false;
  audioSilence();
}

static void audioPlay(const Note* m, uint8_t len) {
  if (!m || len == 0) return;
  melPtr   = m;
  melLen   = len;
  melIdx   = 0;
  melActive = true;
  audioNextNote();
}

static void audioBeep(uint16_t hz, uint16_t ms) {
  static Note n;
  n.hz = hz;
  n.ms = ms;
  audioPlay(&n, 1);
}

/* Call this as often as possible. Non-blocking. */
static void audioUpdate() {
  uint32_t now = micros();

  if (sndHalfUs) {
    if ((int32_t)(now - sndNextEdge) >= 0) {
      sndLevel = !sndLevel;
      digitalWrite(PIN_AUDIO, sndLevel ? HIGH : LOW);
      sndNextEdge += sndHalfUs;
      if ((int32_t)(now - sndNextEdge) > 2000) sndNextEdge = now + sndHalfUs;
    }
  }

  if (melActive && (int32_t)(now - melNoteEnd) >= 0) {
    audioNextNote();
  }
}

/* ===========================================================================
 *  SECTION 4 - BUTTON (debounced, non-blocking)
 *
 *  v2.0 behaviour:
 *    short press (released before BTN_SHORT_MAX_MS) -> next mode
 *    hold for BTN_LONG_MS                            -> emergency stop/resume
 *
 *  The long press fires WHILE the button is still held, so it works even if
 *  the robot is in the middle of a long blocking manoeuvre.
 * ==========================================================================*/

/* Read the button without fighting the 74HC595.
 *
 * If the button is on pin 8, that pin is normally the shift-register data
 * line. The 595 only samples its data input when SRCLK rises (inside
 * srWrite), so we can borrow the pin between writes: make it INPUT_PULLUP,
 * read it, then put it back as OUTPUT LOW. Driving it LOW is deliberate -
 * if the button is held, a LOW output cannot short anything.
 *
 * A 1k resistor in series with the button protects the few microseconds when
 * a write is shifting a 1 and the button closes at exactly that moment.
 */
static bool readButtonRaw() {
#if BTN_MULTIPLEXED
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  delayMicroseconds(10);
  bool pressed = (digitalRead(PIN_BUTTON) == LOW);
  digitalWrite(PIN_BUTTON, LOW);        // pre-load before enabling the driver
  pinMode(PIN_BUTTON, OUTPUT);
  return pressed;
#else
  return (digitalRead(PIN_BUTTON) == LOW);
#endif
}

static void pollButton() {
  if (inputLocked) return;          // boot / self test owns the robot

  uint32_t now = millis();
  if ((uint32_t)(now - btnLastPoll) < 5) return;
  btnLastPoll = now;

  bool raw = readButtonRaw();       // pressed = LOW

  if (raw != btnLastRaw) {          // every edge restarts the debounce timer
    btnLastRaw    = raw;
    btnLastChange = now;
    return;
  }

  if ((uint32_t)(now - btnLastChange) < BTN_DEBOUNCE_MS) return;

  if (raw != btnStable) {
    btnStable = raw;
    if (raw) {                      // press
      btnPressStart = now;
      btnLongFired  = false;
    } else {                        // release
      uint32_t held = now - btnPressStart;
      if (!btnLongFired && held <= BTN_SHORT_MAX_MS) btnShortEvent = true;
#if BTN_MULTIPLEXED
      srRestore = true;             // a press latched an all-zero motor frame
#endif
    }
  }

  /* Long press: fire immediately, no need to release. */
  if (btnStable && !btnLongFired &&
      (uint32_t)(now - btnPressStart) >= BTN_LONG_MS) {
    btnLongFired = true;
    btnLongEvent = true;
  }
}

/* True while a button event is waiting to be consumed by loop().
 * Every blocking manoeuvre checks this so the robot can stop quickly. */
static bool uiEvent() {
  return btnShortEvent || btnLongEvent;
}

/* Short waits that keep audio + button alive. */
static void updateRamp();
static void waitWithAudio(uint16_t ms) {
  uint32_t t0 = millis();
  while ((uint32_t)(millis() - t0) < ms) {
    audioUpdate();
    pollButton();
    updateRamp();
    if (uiEvent()) return;
  }
}

/* ===========================================================================
 *  SECTION 5 - MOTOR DRIVER (74HC595 + 4 PWM lines, no library needed)
 * ==========================================================================*/

static uint8_t srState = 0;

static void srShiftOut() {
  digitalWrite(SR_LATCH, LOW);
  for (int8_t i = 7; i >= 0; --i) {
    digitalWrite(SR_CLK, LOW);
    digitalWrite(SR_DATA, (srState >> i) & 1);
    digitalWrite(SR_CLK, HIGH);
  }
  digitalWrite(SR_LATCH, HIGH);

  /* Leave the data line LOW while idle. The 595 has already latched, so this
   * changes nothing electrically - but it means a button on pin 8 can never
   * be pressing against a driven HIGH between writes. */
  digitalWrite(SR_DATA, LOW);
}

static void srWrite() {
#if BTN_MULTIPLEXED
  /* If the button is down, latch an all-zero frame: pin 8 stays LOW for the
   * whole shift, so a press cannot short an output. srState keeps the real
   * data; the next write after release restores it. */
  if (readButtonRaw()) {
    uint8_t saved = srState;
    srState = 0;
    srShiftOut();
    srState = saved;
    return;
  }
#endif
  srShiftOut();
}

static void wheelWrite(uint8_t bitA, uint8_t bitB, uint8_t pwmPin,
                       int16_t speed, uint8_t invert) {
  if (invert) speed = -speed;

  if (speed > 255)  speed = 255;
  if (speed < -255) speed = -255;

  if (speed > 0) {
    srState |=  (1 << bitA);
    srState &= ~(1 << bitB);
    analogWrite(pwmPin, (uint8_t)speed);
  } else if (speed < 0) {
    srState &= ~(1 << bitA);
    srState |=  (1 << bitB);
    analogWrite(pwmPin, (uint8_t)(-speed));
  } else {
    srState &= ~(1 << bitA);
    srState &= ~(1 << bitB);
    analogWrite(pwmPin, 0);
  }
}

/* left / right in -255..+255 (negative = backwards) */
static void applyDrive(int16_t left, int16_t right) {
  wheelWrite(BIT_M1A, BIT_M1B, PWM_M1, left,  INV_M1);
  wheelWrite(BIT_M2A, BIT_M2B, PWM_M2, left,  INV_M2);
  wheelWrite(BIT_M3A, BIT_M3B, PWM_M3, right, INV_M3);
  wheelWrite(BIT_M4A, BIT_M4B, PWM_M4, right, INV_M4);
  srWrite();
}

static void initMotors() {
  pinMode(SR_LATCH,  OUTPUT);
  pinMode(SR_CLK,    OUTPUT);
  pinMode(SR_ENABLE, OUTPUT);
  pinMode(SR_DATA,   OUTPUT);

  pinMode(PWM_M1, OUTPUT);
  pinMode(PWM_M2, OUTPUT);
  pinMode(PWM_M3, OUTPUT);
  pinMode(PWM_M4, OUTPUT);

  srState = 0;
  srWrite();
  digitalWrite(SR_ENABLE, LOW);      // LOW = 74HC595 outputs enabled
  applyDrive(0, 0);
}

static int16_t approach(int16_t v, int16_t target, uint8_t step) {
  if (v < target) { v += step; if (v > target) v = target; }
  else if (v > target) { v -= step; if (v < target) v = target; }
  return v;
}

static void setTarget(int16_t l, int16_t r) {
  if (l >  255) l =  255;
  if (l < -255) l = -255;
  if (r >  255) r =  255;
  if (r < -255) r = -255;
  tgtL = l;
  tgtR = r;
}

static void updateRamp() {
  uint32_t now = millis();
  if (now - rampLast < RAMP_PERIOD_MS) return;
  rampLast = now;

  /* After a pin-8 button press the latched motor data is all zeros. Put the
   * real frame back as soon as the button is released. */
  if (srRestore) {
    srRestore = false;
    srWrite();
  }

  if (curL == tgtL && curR == tgtR) return;

  curL = approach(curL, tgtL, rampStep);
  curR = approach(curR, tgtR, rampStep);
  applyDrive(curL, curR);
}

/* Hard stop, no ramp. */
static void stopNow() {
  curL = curR = tgtL = tgtR = 0;
  applyDrive(0, 0);
}

/* Block until the ramp has actually reached zero. Without this the robot
 * keeps creeping during the next measurement (very visible in AI mode). */
static void waitUntilStopped(uint16_t maxMs) {
  uint32_t t0 = millis();
  while ((curL != 0 || curR != 0) &&
         (uint32_t)(millis() - t0) < maxMs) {
    waitWithAudio(10);
    if (uiEvent()) return;
  }
  curL = curR = 0;
  applyDrive(0, 0);
}

/* ===========================================================================
 *  SECTION 6 - SERVO + ULTRASONIC
 * ==========================================================================*/

static void servoGoto(uint8_t target, uint8_t stepDeg, uint8_t stepDelayMs) {
  if (target > 180) target = 180;

  while (servoPos != target) {
    if (servoPos < target) {
      uint8_t next = servoPos + stepDeg;
      servoPos = (next > target) ? target : next;
    } else {
      int16_t next = (int16_t)servoPos - (int16_t)stepDeg;
      servoPos = (next < (int16_t)target) ? target : (uint8_t)next;
    }
    scanServo.write(servoPos);

    uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < stepDelayMs) {
      audioUpdate();
      pollButton();
      if (uiEvent()) return;
    }
  }
}

/* Distinguishes the two "no distance" cases that the old code conflated:
 *
 *   ECHO never goes high           -> the sensor is not answering  (DIST_DEAD)
 *   ECHO high but longer than 25ms -> the sensor IS answering, there is just
 *                                     nothing within ~4 m          (DIST_NO_ECHO)
 *
 * A working HC-SR04 holds ECHO high for ~38 ms when it hears no echo. That
 * lets us tell a big empty room from a dead sensor, which is the difference
 * between a safe robot and one that beeps a false fault while scanning a
 * corridor. We also wait for that 38 ms pulse to end, otherwise the next TRIG
 * is ignored and the following sample would look dead.
 */
static uint16_t readDistanceRawCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(3);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  uint32_t t0 = micros();
  while (digitalRead(PIN_ECHO) == LOW) {
    audioUpdate();
    if ((uint32_t)(micros() - t0) > ECHO_TIMEOUT_US) return DIST_DEAD;
  }

  uint32_t t1 = micros();
  while (digitalRead(PIN_ECHO) == HIGH) {
    audioUpdate();
    if ((uint32_t)(micros() - t1) > ECHO_TIMEOUT_US) {
      /* It answered, but nothing reflected: wait for the module's own
       * ~38 ms timeout pulse to finish so the next ping is accepted. */
      uint32_t t2 = micros();
      while (digitalRead(PIN_ECHO) == HIGH) {
        audioUpdate();
        if ((uint32_t)(micros() - t2) > 40000UL) break;
      }
      return DIST_NO_ECHO;
    }
  }

  uint32_t dur = micros() - t1;
  if (dur < 60) return DIST_NO_ECHO;   // noise spike, but the sensor answered

  uint16_t cm = (uint16_t)(dur / 58UL);
  if (cm > DIST_MAX_CM) cm = DIST_MAX_CM;
  return cm;
}

/* ---- ultrasonic health (v2.0) --------------------------------------------
 * A timeout is not a distance. If the sensor goes silent, the robot must
 * stop. This is the single most important safety change in v2.0.
 * ==========================================================================*/

static void noteSensorAlive() {
  lastValidEchoMs = millis();
  sensorSeenEver  = true;

  if (sensorFault) {                // recovered
    sensorFault = false;
    audioBeep(1600, 70);
  }
}

static void triggerSensorFault() {
  if (sensorFault) return;          // already latched
  sensorFault = true;
  stopNow();
  audioPlay(MEL_SENSOR_FAULT, sizeof(MEL_SENSOR_FAULT) / sizeof(Note));
}

/* Interpret the latest measurement.
 *   a distance, or "nothing in range" -> the sensor answered -> OK
 *   no answer, still starting          -> WAIT  (caller must stay stopped)
 *   no answer, sensor was alive        -> OK    (short dropout; caller's
 *                                                policy decides, but a dead
 *                                                reading never means "clear")
 *   no answer for too long             -> FAULT (caller must stop)
 */
static SensorState sensorState(uint16_t d) {
  if (d != DIST_DEAD) {             // it answered (distance OR empty space)
    noteSensorAlive();
    return SENSOR_OK;
  }

  if (sensorFault) return SENSOR_FAULT;                         // latched

  uint32_t now = millis();

  if (!sensorSeenEver) {
    if ((uint32_t)(now - sensorBootMs) > SENSOR_BOOT_WAIT_MS) {
      triggerSensorFault();
      return SENSOR_FAULT;
    }
    return SENSOR_WAIT;
  }

  if ((uint32_t)(now - lastValidEchoMs) > SENSOR_GRACE_MS) {
    triggerSensorFault();
    return SENSOR_FAULT;
  }

  return SENSOR_OK;
}

/* Median of the VALID samples only. The old code mixed 999 (timeout) into
 * the median, so two missed pings could turn a real 20 cm wall into
 * "no echo = clear" and the robot drove into it. */
static uint16_t measureCm(uint8_t samples) {
  uint16_t v[3];
  uint8_t n = 0;
  bool alive = false;

  if (samples < 1) samples = 1;
  if (samples > 3) samples = 3;

  for (uint8_t i = 0; i < samples; i++) {
    uint16_t d = readDistanceRawCm();
    if (d != DIST_DEAD) {
      alive = true;
      noteSensorAlive();
      if (d > 0 && d < DIST_NO_ECHO && n < 3) v[n++] = d;
    }
    if (i + 1 < samples) waitWithAudio(12);
  }

  if (n == 0) return alive ? DIST_NO_ECHO : DIST_DEAD;
  if (n == 1) return v[0];
  if (n == 2) return (v[0] < v[1]) ? v[0] : v[1];

  uint16_t x = v[0], y = v[1], z = v[2];
  if (x > y) { uint16_t t = x; x = y; y = t; }
  if (y > z) { uint16_t t = y; y = z; z = t; }
  if (x > y) { uint16_t t = x; x = y; y = t; }
  return y;
}

static void servoCenter() {
  servoGoto(ANGLE_CENTER, 4, 6);
}

/* ===========================================================================
 *  SECTION 7 - SHARED MANOEUVRES
 * ==========================================================================*/

static void pivot(int8_t dir, uint8_t speed, uint16_t ms, uint8_t step) {
  /* dir = +1 -> turn right (left wheel forward, right wheel backward)
     dir = -1 -> turn left                                              */
  rampStep = step;
  setTarget(dir > 0 ?  (int16_t)speed : -(int16_t)speed,
            dir > 0 ? -(int16_t)speed :  (int16_t)speed);
  waitWithAudio(ms);
  setTarget(0, 0);
  waitUntilStopped(900);
  waitWithAudio(80);
}

static void driveStraight(int16_t speed, uint16_t ms, uint8_t step) {
  rampStep = step;
  setTarget(speed, speed);
  waitWithAudio(ms);
  setTarget(0, 0);
  waitUntilStopped(900);
  waitWithAudio(80);
}

/* ===========================================================================
 *  SECTION 8 - MODE 1 : OBSTACLE AVOIDANCE
 * ==========================================================================*/

static uint32_t obsLastSample = 0;
static uint16_t obsFront      = DIST_NO_ECHO;

/* Returns true if a mode change interrupted the manoeuvre. */
static bool obstacleDecision() {
  /* A. stop + single beep (your spec) */
  stopNow();
  audioBeep(1200, 90);
  waitWithAudio(120);
  if (uiEvent()) return true;

  /* B. scan right 90 deg */
  servoGoto(SIDE_RIGHT_ANGLE, 5, 7);
  waitWithAudio(130);
  if (uiEvent()) return true;
  uint16_t dR = measureCm(3);

  /* C. scan left 90 deg */
  servoGoto(SIDE_LEFT_ANGLE, 5, 7);
  waitWithAudio(130);
  if (uiEvent()) return true;
  uint16_t dL = measureCm(3);

  /* If the sensor died during the scan, do not act on garbage readings. */
  if (dR == DIST_DEAD || dL == DIST_DEAD ||
      sensorState(dR) == SENSOR_FAULT || sensorState(dL) == SENSOR_FAULT) {
    servoGoto(ANGLE_CENTER, 5, 7);   // do not leave the head pointing sideways
    return true;
  }

  /* D. back to centre */
  servoGoto(ANGLE_CENTER, 5, 7);
  waitWithAudio(80);

  bool rightFree = (dR > OBSTACLE_RESUME_CM);
  bool leftFree  = (dL > OBSTACLE_RESUME_CM);

  DBG("OBS R="); DBG(dR); DBG(" L="); DBGLN(dL);

  /* E. choose - exactly your rule:
   *    right blocked -> go left, left blocked -> go right,
   *    both clear -> take the wider gap, both blocked -> reverse + 180 */
  if (rightFree && leftFree) {
    if (dR >= dL) { pivot(+1, OBSTACLE_TURN_SPEED, TURN90_MS, RAMP_STEP_OBSTACLE); }
    else          { pivot(-1, OBSTACLE_TURN_SPEED, TURN90_MS, RAMP_STEP_OBSTACLE); }
  } else if (rightFree) {
    pivot(+1, OBSTACLE_TURN_SPEED, TURN90_MS, RAMP_STEP_OBSTACLE);
  } else if (leftFree) {
    pivot(-1, OBSTACLE_TURN_SPEED, TURN90_MS, RAMP_STEP_OBSTACLE);
  } else {
    /* both sides blocked: back off, spin ~180, try again */
    audioBeep(320, 110);
    driveStraight(-OBSTACLE_BACK_SPEED, REVERSE_MS, RAMP_STEP_OBSTACLE);
    if (uiEvent()) return true;

    int8_t dir = (dL >= dR) ? -1 : +1;
    pivot(dir, OBSTACLE_TURN_SPEED, TURN180_MS, RAMP_STEP_OBSTACLE);
    return uiEvent();
  }

  if (uiEvent()) return true;

  /* F. verify the new heading, escape if it is still blocked */
  waitWithAudio(60);
  uint16_t dF = measureCm(2);
  if (dF == DIST_DEAD) return true;          // sensor dropout: do not guess
  if (dF <= OBSTACLE_STOP_CM) {
    driveStraight(-OBSTACLE_BACK_SPEED, REVERSE_MS, RAMP_STEP_OBSTACLE);
    if (uiEvent()) return true;
    pivot(+1, OBSTACLE_TURN_SPEED, TURN90_MS, RAMP_STEP_OBSTACLE);
  }
  return uiEvent();
}

static void runObstacle() {
  rampStep = RAMP_STEP_OBSTACLE;

  if ((uint32_t)(millis() - obsLastSample) < 55) return;
  obsLastSample = millis();

  obsFront = measureCm(3);

  SensorState st = sensorState(obsFront);

  if (st == SENSOR_FAULT) {
    stopNow();                          // never drive blind
    obsLastSample = millis();
    return;
  }
  if (st == SENSOR_WAIT) {
    setTarget(0, 0);                    // wait for the first echo
    return;
  }

  if (obsFront == DIST_DEAD) {
    /* One dead sample inside the grace window: hold still, never treat 0 as
     * "wall at 0 cm" (that would start a turn) or as "clear". */
    setTarget(0, 0);
    return;
  }

  if (obsFront == DIST_NO_ECHO) {
    /* Nothing in range (or a dropped ping). By default: stop and re-ping,
     * because "cannot see" must never mean "clear road". */
#if NO_ECHO_MEANS_CLEAR
    setTarget(OBSTACLE_SLOW_SPEED, OBSTACLE_SLOW_SPEED);
#else
    setTarget(0, 0);
#endif
  } else if (obsFront <= OBSTACLE_STOP_CM) {
    setTarget(0, 0);
    waitUntilStopped(600);              // let it actually stop
    if (uiEvent()) return;
    obstacleDecision();
    obsLastSample = millis();           // don't instantly re-measure
  } else if (obsFront < OBSTACLE_SLOW_CM) {
    setTarget(OBSTACLE_SLOW_SPEED, OBSTACLE_SLOW_SPEED);
  } else {
    setTarget(OBSTACLE_SPEED, OBSTACLE_SPEED);
  }
}

/* ===========================================================================
 *  SECTION 9 - MODE 2 : BLUETOOTH
 * ==========================================================================*/

static void btDrive(int16_t l, int16_t r) {
  rampStep = RAMP_STEP_BT;
  setTarget(l, r);
  curL = l;
  curR = r;
  applyDrive(curL, curR);
}

static void btStop() {
  btDrive(0, 0);
  btMoving = false;
}

static void btSetSpeedFromChar(char c) {
  /* "Bluetooth RC Car" app: '0'..'9' and 'q' */
  if (c >= '0' && c <= '9') {
    uint8_t d = (uint8_t)(c - '0');
    if (d == 0) { btSpeed = 0; btStop(); return; }
    btSpeed = (uint8_t)(BT_MIN_SPEED +
              ((uint16_t)(BT_MAX_SPEED - BT_MIN_SPEED) * d) / 9);
  } else if (c == 'q' || c == 'Q') {
    btSpeed = BT_MAX_SPEED;
  }
}

static void handleBtCommand(char c) {
  int16_t s = btSpeed;
  if (s == 0) s = BT_MAX_SPEED;   // safety: never stay at 0 if a move arrives

  switch (c) {
    /* --- standard "Bluetooth RC Car" protocol --- */
    case 'F': btDrive( s,  s); btMoving = true; break;
    case 'B': btDrive(-s, -s); btMoving = true; break;
    case 'L': btDrive(-s,  s); btMoving = true; break;
    case 'R': btDrive( s, -s); btMoving = true; break;
    case 'G': btDrive( s / 3,  s); btMoving = true; break;   // fwd-left
    case 'I': btDrive( s,  s / 3); btMoving = true; break;   // fwd-right
    case 'H': btDrive(-s / 3, -s); btMoving = true; break;   // back-left
    case 'J': btDrive(-s, -s / 3); btMoving = true; break;   // back-right
    case 'S': btStop(); break;
    case 'X': btStop(); break;

    /* --- some apps send lowercase letters --- */
    case 'f': btDrive( s,  s); btMoving = true; break;
    case 'b': btDrive(-s, -s); btMoving = true; break;
    case 'l': btDrive(-s,  s); btMoving = true; break;
    case 'r': btDrive( s, -s); btMoving = true; break;

    default:
      btSetSpeedFromChar(c);
      break;
  }

  btLastCmd = millis();
}

/* ---- v2.0: accept whole words as well as the single-character protocol ---- */

static bool wordEq(const char* a, const char* b) {
  while (*a && *b) {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
    if (ca != cb) return false;
  }
  return (*a == '\0') && (*b == '\0');
}

static bool isSingleCmd(char c) {
  if (c >= '0' && c <= '9') return true;
  switch (c) {
    case 'F': case 'B': case 'L': case 'R':
    case 'G': case 'I': case 'H': case 'J':
    case 'S': case 'X':
    case 'f': case 'b': case 'l': case 'r':
    case 'q': case 'Q':
      return true;
    default:
      return false;
  }
}

/* Only a recognised word refreshes the failsafe timer. Random text must not
 * keep the motors running. */
static bool btHandleWord(const char* w) {
  int16_t s = (btSpeed == 0) ? (int16_t)BT_MAX_SPEED : (int16_t)btSpeed;
  bool ok = true;

  if (wordEq(w, "forward") || wordEq(w, "fwd") || wordEq(w, "up") || wordEq(w, "go")) {
    btDrive(s, s); btMoving = true;
  } else if (wordEq(w, "back") || wordEq(w, "backward") || wordEq(w, "reverse") || wordEq(w, "rev") || wordEq(w, "down")) {
    btDrive(-s, -s); btMoving = true;
  } else if (wordEq(w, "left")) {
    btDrive(-s, s); btMoving = true;
  } else if (wordEq(w, "right")) {
    btDrive(s, -s); btMoving = true;
  } else if (wordEq(w, "stop") || wordEq(w, "halt")) {
    btStop();
  } else if (wordEq(w, "forwardleft") || wordEq(w, "forward-left") || wordEq(w, "fl")) {
    btDrive(s / 3, s); btMoving = true;
  } else if (wordEq(w, "forwardright") || wordEq(w, "forward-right") || wordEq(w, "fr")) {
    btDrive(s, s / 3); btMoving = true;
  } else if (wordEq(w, "backleft") || wordEq(w, "back-left") || wordEq(w, "bl")) {
    btDrive(-s / 3, -s); btMoving = true;
  } else if (wordEq(w, "backright") || wordEq(w, "back-right") || wordEq(w, "br")) {
    btDrive(-s, -s / 3); btMoving = true;
  } else {
    ok = false;
  }

  if (ok) btLastCmd = millis();
  return ok;
}

#define BT_BUF_MAX   12
static char     btBuf[BT_BUF_MAX];
static uint8_t  btBufLen = 0;
static uint32_t btBufMs  = 0;

static void btFlushBuffer() {
  if (btBufLen == 0) return;
  btBuf[btBufLen] = '\0';

  if (btBufLen == 1) {
    if (isSingleCmd(btBuf[0])) handleBtCommand(btBuf[0]);
    btBufLen = 0;
    return;
  }

  bool allSame   = true;
  bool allSingle = true;
  for (uint8_t i = 0; i < btBufLen; i++) {
    if (btBuf[i] != btBuf[0]) allSame = false;
    if (!isSingleCmd(btBuf[i])) allSingle = false;
  }

  if (allSame) {
    /* The app is repeating one key while it is held: FFFFF... */
    if (isSingleCmd(btBuf[0])) handleBtCommand(btBuf[0]);
  } else if (!btHandleWord(btBuf)) {
    /* Not a word. If it is a burst of different single keys, last one wins. */
    if (allSingle) handleBtCommand(btBuf[btBufLen - 1]);
  }

  btBufLen = 0;
}

static void btFeed(char c) {
  btBufMs = millis();

  if (c == '\n' || c == '\r') { btFlushBuffer(); return; }

  if (btBufLen >= BT_BUF_MAX - 1) btFlushBuffer();
  btBuf[btBufLen++] = c;
}

static void flushBt() {
  uint32_t t0 = millis();
  while ((uint32_t)(millis() - t0) < 80) {
    while (BT_PORT.available()) (void)BT_PORT.read();
    audioUpdate();
  }
  btBufLen = 0;
}

static void runBluetooth() {
  rampStep = RAMP_STEP_BT;

  while (BT_PORT.available()) btFeed((char)BT_PORT.read());

  /* A single-character app sends no newline: flush after a short idle. */
  if (btBufLen && (uint32_t)(millis() - btBufMs) > 35) btFlushBuffer();

  if (btMoving && (uint32_t)(millis() - btLastCmd) > BT_FAILSAFE_MS) {
    btStop();
  }

#if BT_COLLISION_GUARD
  static uint32_t lastGuard = 0;
  if ((uint32_t)(millis() - lastGuard) > 60) {
    lastGuard = millis();
    if (btMoving && readDistanceRawCm() <= BT_GUARD_CM) {
      btStop();
    }
  }
#endif
}

/* ===========================================================================
 *  SECTION 10 - MODE 3 : AI (slow, smooth, 5-zone decision making)
 * ==========================================================================*/

static uint16_t aiMeasureAt(uint8_t idx, uint8_t stepDeg, uint8_t stepMs) {
  servoGoto(AI_ANGLES[idx], stepDeg, stepMs);
  waitWithAudio(85);
  return measureCm(3);
}

static void aiScan() {
  aiDist[0] = aiMeasureAt(0, 3, 5);
  aiDist[1] = aiMeasureAt(1, 3, 5);
  aiDist[2] = aiMeasureAt(2, 3, 5);
  aiDist[3] = aiMeasureAt(3, 3, 5);
  aiDist[4] = aiMeasureAt(4, 3, 5);
  servoGoto(ANGLE_CENTER, 3, 5);
  waitWithAudio(60);
}

static void aiSteerForward(int16_t base) {
  int16_t steer = (int16_t)((aiBias * AI_STEER_GAIN) / 6);
  if (steer >  26) steer =  26;
  if (steer < -26) steer = -26;

  int16_t l = base + steer;      // aiBias > 0 = right is more open -> curve right
  int16_t r = base - steer;

  if (l < -255) l = -255;
  if (r < -255) r = -255;
  if (l >  255) l =  255;
  if (r >  255) r =  255;

  setTarget(l, r);

  /* bias fades out so the robot recentres itself */
  aiBias = (int16_t)((aiBias * 7) / 8);
}

static void aiDecideAndAct() {
  /* If the sensor went silent during the 5-point scan, stop instead of
   * trusting five 999s as "wide open space". */
  if (sensorState(aiDist[2]) != SENSOR_OK) {
    stopNow();
    return;
  }

  /* A dead front sample must not fall into the "0 cm = blocked, reverse"
   * path or the "999 = wide open" path. Hold still and re-ping. */
  if (aiDist[2] == DIST_DEAD) {
    stopNow();
    return;
  }

  uint16_t front = aiDist[2];
  uint16_t wl    = (uint16_t)((aiDist[3] * 2 + aiDist[4]) / 3);  // left weight
  uint16_t wr    = (uint16_t)((aiDist[0] * 2 + aiDist[1]) / 3);  // right weight

  DBG("AI F="); DBG(front); DBG(" L="); DBG(wl); DBG(" R="); DBGLN(wr);

  /* 1. the path opened up while we were scanning */
  if (front > AI_CLEAR_MIN_CM) {
    audioPlay(MEL_AI_CLEAR, sizeof(MEL_AI_CLEAR) / sizeof(Note));
    aiBias = 0;
    setTarget(AI_CRUISE_SPEED, AI_CRUISE_SPEED);
    return;
  }

  bool lFree = (wl > AI_CLEAR_MIN_CM);
  bool rFree = (wr > AI_CLEAR_MIN_CM);

  /* 2. completely boxed in -> reverse, then pivot toward the wider side */
  if (!lFree && !rFree) {
    audioPlay(MEL_AI_BLOCKED, sizeof(MEL_AI_BLOCKED) / sizeof(Note));
    setTarget(-AI_BACK_SPEED, -AI_BACK_SPEED);
    waitWithAudio(AI_BACKUP_MS);
    if (uiEvent()) return;
    setTarget(0, 0);
    waitUntilStopped(900);
    if (uiEvent()) return;

    audioPlay(MEL_AI_BACK, sizeof(MEL_AI_BACK) / sizeof(Note));
    int8_t dir = (wl >= wr) ? -1 : +1;
    pivot(dir, AI_TURN_SPEED, TURN90_MS, RAMP_STEP_AI);
    aiLastTurn = dir;
    aiBias = (dir > 0) ? 18 : -18;     // keep curving toward the side we opened
    if (uiEvent()) return;

    /* second chance: if still blocked, do a second 90 deg */
    uint16_t again = measureCm(2);
    if (again <= AI_STOP_DIST_CM) {
      pivot(dir, AI_TURN_SPEED, TURN90_MS, RAMP_STEP_AI);
      aiLastTurn = dir;
    }
    return;
  }

  /* 3. pick a side, with a little momentum so it does not dither */
  int8_t dir;
  if (lFree && rFree) {
    int16_t diff = (int16_t)wl - (int16_t)wr;
    if (diff < 0) diff = -diff;
    if (diff < 12 && aiLastTurn != 0) {
      dir = aiLastTurn;                // keep the current direction
    } else {
      dir = (wl > wr) ? -1 : +1;
    }
  } else {
    dir = lFree ? -1 : +1;
  }

  pivot(dir, AI_TURN_SPEED, TURN90_MS, RAMP_STEP_AI);
  aiLastTurn = dir;
  aiBias = (dir > 0) ? 14 : -14;
}

static void runAI() {
  rampStep = RAMP_STEP_AI;

  if ((uint32_t)(millis() - aiLastSample) < AI_SAMPLE_MS) return;
  aiLastSample = millis();

  uint16_t d = measureCm(3);

  SensorState st = sensorState(d);

  if (st == SENSOR_FAULT) {
    stopNow();                       // never drive blind
    return;
  }
  if (st == SENSOR_WAIT) {
    setTarget(0, 0);                 // wait for the first echo
    return;
  }

  if (d == DIST_DEAD) {
    /* One dead sample inside the grace window: hold still. Never let 0 cm
     * trigger the "d < AI_STOP_DIST_CM" branch. */
    setTarget(0, 0);
    return;
  }

  if (d == DIST_NO_ECHO) {           // healthy sensor, nothing in range
#if NO_ECHO_MEANS_CLEAR
    aiSteerForward(AI_CRUISE_SPEED);
#else
    setTarget(0, 0);
#endif
    return;
  }

  if (d > AI_SLOW_DIST_CM) {
    aiSteerForward(AI_CRUISE_SPEED);
    return;
  }

  if (d > AI_STOP_DIST_CM) {
    /* ease off proportionally instead of slamming on the brakes */
    uint16_t span = (uint16_t)(AI_SLOW_DIST_CM - AI_STOP_DIST_CM);
    uint16_t into = (uint16_t)(d - AI_STOP_DIST_CM);
    int16_t  base = (int16_t)(AI_MIN_SPEED +
                     ((int32_t)(AI_CRUISE_SPEED - AI_MIN_SPEED) * into) / span);
    aiSteerForward(base);
    return;
  }

  /* too close: full stop, think, scan, act */
  setTarget(0, 0);
  waitUntilStopped(900);
  if (uiEvent()) return;
  waitWithAudio(120);
  if (uiEvent()) return;

  audioPlay(MEL_AI_THINK, sizeof(MEL_AI_THINK) / sizeof(Note));
  aiScan();
  if (uiEvent()) return;
  aiDecideAndAct();
}

/* ===========================================================================
 *  SECTION 11 - MODE SWITCHING / STATUS LED / SETUP / LOOP
 * ==========================================================================*/

static void enterMode(Mode m) {
  stopNow();
  servoCenter();
  aiBias = 0;
  aiLastTurn = 0;
  aiLastSample = millis();

  switch (m) {
    case MODE_OBSTACLE:
#if MODE_CHANGE_CHIRP
      audioPlay(MEL_TO_OBSTACLE, sizeof(MEL_TO_OBSTACLE) / sizeof(Note));
#endif
      obsLastSample = millis();
      break;

    case MODE_BLUETOOTH:
      flushBt();
      btSpeed  = BT_MAX_SPEED;
      btMoving = false;
      btLastCmd = millis();
#if MODE_CHANGE_CHIRP
      audioPlay(MEL_TO_BT, sizeof(MEL_TO_BT) / sizeof(Note));
#endif
      break;

    case MODE_AI:
#if MODE_CHANGE_CHIRP
      audioPlay(MEL_TO_AI, sizeof(MEL_TO_AI) / sizeof(Note));
#endif
      aiLastSample = millis();
      break;
  }

  DBG("MODE="); DBGLN((uint8_t)m);
}

static void nextMode() {
  mode = (Mode)(((uint8_t)mode + 1) % 3);
  enterMode(mode);
}

static void updateStatusLed() {
  uint32_t now = millis();

  if (halted) {
    /* rapid double blink: latched OFF by the long press */
    uint32_t ph = now % 700UL;
    digitalWrite(PIN_LED, (ph < 90 || (ph >= 180 && ph < 270)) ? HIGH : LOW);
    return;
  }

  if (sensorFault && mode != MODE_BLUETOOTH) {
    /* fast flicker: ultrasonic fault, robot refuses to drive */
    digitalWrite(PIN_LED, (((now / 120UL) & 1UL) != 0) ? HIGH : LOW);
    return;
  }

  uint32_t ph = now % 2400UL;
  uint8_t  n  = (uint8_t)mode + 1;      // 1, 2 or 3 flashes
  bool on = false;

  for (uint8_t i = 0; i < n; i++) {
    uint16_t s = (uint16_t)(i * 240);
    if (ph >= s && ph < (uint16_t)(s + 110)) { on = true; break; }
  }
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

/* ===========================================================================
 *  FACTORY SELF TEST  -  hold the button while powering on
 *  M1, M2, M3, M4 are driven one at a time, then both sides, then the servo.
 *  Use it to confirm INV_M1..4 and that every wheel is wired correctly.
 * ==========================================================================*/

static void motorOnly(uint8_t idx, int16_t spd) {
  applyDrive(0, 0);
  switch (idx) {
    case 0: wheelWrite(BIT_M1A, BIT_M1B, PWM_M1, spd, INV_M1); break;
    case 1: wheelWrite(BIT_M2A, BIT_M2B, PWM_M2, spd, INV_M2); break;
    case 2: wheelWrite(BIT_M3A, BIT_M3B, PWM_M3, spd, INV_M3); break;
    case 3: wheelWrite(BIT_M4A, BIT_M4B, PWM_M4, spd, INV_M4); break;
  }
  srWrite();
}

static void selfTest() {
  inputLocked = true;
  stopNow();

  audioBeep(1000, 150);
  waitWithAudio(250);
  audioBeep(1500, 150);
  waitWithAudio(350);

  /* The self test is started by HOLDING the button. If that button is on
   * pin 8, the motor data line must not be driven while it is still held.
   * Wait (up to 8 s) for the user to let go, then run the motor checks. */
  uint32_t relT0 = millis();
  while (readButtonRaw() && (uint32_t)(millis() - relT0) < 8000UL) {
    audioUpdate();
  }
  if (readButtonRaw()) {              // still held: skip the motor test
    audioBeep(400, 300);
    stopNow();
    return;
  }
  waitWithAudio(150);

  /* every motor forward, one at a time: M1 M2 M3 M4 */
  for (uint8_t i = 0; i < 4; i++) {
    motorOnly(i, 160);
    waitWithAudio(600);
    motorOnly(i, 0);
    waitWithAudio(250);
  }

  /* both sides together */
  applyDrive(160, 160);
  waitWithAudio(900);
  applyDrive(0, 0);
  waitWithAudio(300);

  applyDrive(-160, -160);
  waitWithAudio(900);
  applyDrive(0, 0);
  waitWithAudio(400);

  /* servo end points (the sensor should look right, ahead, left) */
  servoGoto(SIDE_RIGHT_ANGLE, 4, 8);
  waitWithAudio(400);
  servoGoto(ANGLE_CENTER, 4, 8);
  waitWithAudio(400);
  servoGoto(SIDE_LEFT_ANGLE, 4, 8);
  waitWithAudio(400);
  servoGoto(ANGLE_CENTER, 4, 8);
  waitWithAudio(300);

  audioPlay(MEL_AI_CLEAR, sizeof(MEL_AI_CLEAR) / sizeof(Note));
  waitWithAudio(400);

  /* setup() releases inputLocked after the boot sequence. */
}

/* Power-on animation: servo sweeps right/left for 3 s while music plays. */
static void bootSequence() {
  audioPlay(MEL_BOOT, sizeof(MEL_BOOT) / sizeof(Note));

  servoGoto(ANGLE_CENTER, 2, 4);
  waitWithAudio(80);

  uint32_t t0 = millis();
  bool toRight = true;

  while ((uint32_t)(millis() - t0) < BOOT_TIME_MS) {
    servoGoto(toRight ? SIDE_RIGHT_ANGLE : SIDE_LEFT_ANGLE, 3, 6);
    toRight = !toRight;
    if (uiEvent()) break;
  }

  servoGoto(ANGLE_CENTER, 3, 5);
  waitWithAudio(120);

  /* Prove the HC-SR04 answers before the motors are ever allowed to run.
   * measureCm() records a valid echo in the sensor-health state. */
  (void)measureCm(1);
  waitWithAudio(40);
  (void)measureCm(1);
}

/* ===========================================================================
 *  EMERGENCY STOP / RESUME  (long press)
 * ==========================================================================*/

static void haltRobot() {
  halted = true;
  stopNow();
  audioPlay(MEL_HALT, sizeof(MEL_HALT) / sizeof(Note));
  DBGLN("HALT");
}

static void resumeRobot() {
  halted = false;
  enterMode(mode);                 // recentre servo, clear ramp, chirp
  DBGLN("RESUME");
}

/* ===========================================================================*/

void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_BUTTON, INPUT_PULLUP);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  pinMode(PIN_AUDIO, OUTPUT);
  digitalWrite(PIN_AUDIO, LOW);

  inputLocked  = true;             // no button events while the robot boots
  sensorBootMs = millis();

  initMotors();

  /* HC-SR04 needs ~50 ms after power-up before the first ping */
  waitWithAudio(60);

  scanServo.attach(PIN_SERVO);
  servoPos = ANGLE_CENTER;
  scanServo.write(servoPos);
  waitWithAudio(250);

  /* Hold the button while switching on to run the hardware self test.
   * readButtonRaw() works whether the button is on pin 2 or multiplexed on
   * pin 8 (initMotors() has already taken pin 8 as an output). */
  waitWithAudio(80);
  if (readButtonRaw()) {
    selfTest();
    waitWithAudio(200);
  }

#if BT_USE_SOFTWARE_SERIAL
  btPort.begin(BT_BAUD);
  #if ENABLE_DEBUG
    Serial.begin(115200);
  #endif
#else
  Serial.begin(BT_BAUD);      // HC-05 / HC-06
#endif

  /* Short proof-of-life line. With BT on pins 0/1 it goes to the module,
   * which ignores it; with the module unplugged you can read it at 9600. */
  BT_PORT.println(F("BOLT v2.0 READY"));
  BT_PORT.println(F("BTN:A3-GND  PIN8=motor data, never a button"));

  bootSequence();

  mode          = MODE_OBSTACLE;
  obsLastSample = millis();
  aiLastSample  = millis();
  rampStep      = RAMP_STEP_OBSTACLE;
  setTarget(0, 0);

  /* Resync the button with reality. If the user is still holding it at this
   * instant, do not fire a long press immediately. */
  inputLocked   = false;
  btnLastRaw    = readButtonRaw();
  btnStable     = btnLastRaw;
  btnPressStart = millis();
  btnLastChange = millis();
  btnLastPoll   = millis();
  btnLongFired  = btnLastRaw;
  btnShortEvent = false;
  btnLongEvent  = false;
  halted        = false;

  sensorBootMs = millis();         // start the "first echo" grace period now

  DBGLN("BOLT UNO ROBOT ready");
}

void loop() {
  pollButton();

  /* Button events come first: they must never wait for a manoeuvre. */
  if (btnLongEvent) {
    btnLongEvent = false;
    btnShortEvent = false;
    if (halted) resumeRobot();
    else        haltRobot();
  } else if (btnShortEvent) {
    btnShortEvent = false;
    if (halted) {
      resumeRobot();               // first press only wakes the robot
    } else {
      stopNow();
      nextMode();
    }
  }

  if (halted) {
    if (curL || curR || tgtL || tgtR) stopNow();
    updateRamp();
    audioUpdate();
    updateStatusLed();
    return;
  }

  switch (mode) {
    case MODE_OBSTACLE:  runObstacle();   break;
    case MODE_BLUETOOTH: runBluetooth();  break;
    case MODE_AI:        runAI();         break;
  }

  updateRamp();
  audioUpdate();
  updateStatusLed();
}
