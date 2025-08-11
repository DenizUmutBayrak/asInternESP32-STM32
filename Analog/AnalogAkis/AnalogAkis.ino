const int ledPin = 4;
const int buttonPin = 15;

int brightness = 0;
int fadeAmount = 5;

unsigned long previousMillis = 0;
const long interval = 30;  // parlama hız aralığı (milisaniye)

void setup() {
  pinMode(ledPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
}

void loop() {
  bool buttonPressed = digitalRead(buttonPin) == LOW;
  unsigned long currentMillis = millis();

  // Parlama döngüsü her zaman çalışır
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    brightness += fadeAmount;
    if (brightness <= 0 || brightness >= 255) {
      fadeAmount = -fadeAmount;
    }
  }

  // Buton basılıysa LED tam parlak, değilse parlama değerinde
  if (buttonPressed) {
    analogWrite(ledPin, 255);
  } else {
    analogWrite(ledPin, brightness);
  }
}
