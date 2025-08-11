#define LED_PIN     2
#define BUTTON_PIN  1

bool led = 0, blink = 0, lastBtn = 1;
unsigned long tPress = 0, tBlink = 0;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop() {
  bool btn = digitalRead(BUTTON_PIN);
  unsigned long tNow = millis();

  if (!btn && lastBtn) tPress = tNow;                       // Rising edge
  if (btn && !lastBtn) {                                    // Falling edge
    if (tNow - tPress < 500) {
      if (blink) { blink = 0; led = 0; digitalWrite(LED_PIN, 0); }
      else       { led ^= 1; digitalWrite(LED_PIN, led); }
    } else {
      blink = 1; tBlink = tNow;
    }
  }

  if (blink && tNow - tBlink >= 300) {
    tBlink = tNow;
    led ^= 1;
    digitalWrite(LED_PIN, led);
  }

  lastBtn = btn;
}
