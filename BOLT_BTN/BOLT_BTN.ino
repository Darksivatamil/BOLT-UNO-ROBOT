/* ============================================================================
 *  BOLT UNO ROBOT - BUTTON-ONLY TEST
 *  ---------------------------------------------------------------------------
 *  Reads A3 (button to GND, INPUT_PULLUP) and mirrors it to the on-board LED:
 *
 *      released -> LED OFF
 *      pressed  -> LED ON
 *
 *  Also prints BTN=up / BTN=DOWN at 9600 baud.
 *
 *  If the LED never lights, the switch (or its wiring) is not closing.
 *  Touch a jumper from A3 to GND: the LED must light.
 * ==========================================================================*/

#define PIN_BUTTON A3
#define PIN_LED    13

void setup() {
  Serial.begin(9600);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  delay(300);
  Serial.println();
  Serial.println(F("BUTTON TEST - A3 to GND"));
  Serial.println(F("LED OFF = up,  LED ON = pressed"));
}

void loop() {
  static bool last = false;
  static uint32_t lastPrint = 0;

  bool down = (digitalRead(PIN_BUTTON) == LOW);
  digitalWrite(PIN_LED, down ? HIGH : LOW);

  if (down != last || (uint32_t)(millis() - lastPrint) >= 1000UL) {
    last = down;
    lastPrint = millis();
    Serial.print(F("BTN="));
    Serial.println(down ? F("DOWN") : F("up"));
  }
  delay(20);
}
