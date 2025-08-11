#include "DHT.h"
#include <Arduino.h>

#define DHT_PIN 18           // DHT11 sensör pini
#define DHT_TYPE DHT11       // Sensör tipi
#define LED_PIN 5            // PWM ile kontrol edilen LED pini

DHT dht(DHT_PIN, DHT_TYPE);

// PWM ayarları
const int freq = 5000;         // PWM frekansı (Hz)
const int ledChannel = 0;      // Kanal numarası (0–15)
const int resolution = 8;      // 8-bit çözünürlük: 0–255 arası değer

void setup() {
  Serial.begin(115200);
  dht.begin();

  // LED için PWM yapılandırması
  ledcSetup(ledChannel, freq, resolution);
  ledcAttachPin(LED_PIN, ledChannel);
}

void loop() {
  delay(2000);  // DHT11 için uygun okuma aralığı

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Hata: DHT11 sensöründen veri okunamadı!");
  } else {
    Serial.print("Nem: ");
    Serial.print(humidity, 1);
    Serial.print(" %\t");
    Serial.print("Sıcaklık: ");
    Serial.print(temperature, 1);
    Serial.println(" °C");

    // Parlaklık hesaplama (nem bazlı)
    int brightness;

    if (humidity <= 21) {
      brightness = 50;  // Düşük parlaklık
    } else if (humidity >= 80) {
      brightness = 255; // Maksimum parlaklık
    } else {
      // 21 ile 80 arası için lineer artış
      brightness = map(humidity, 21, 80, 50, 255);
    }

    Serial.print("LED PWM Parlaklık: ");
    Serial.println(brightness);

    // LED'e PWM değeri gönder
    ledcWrite(ledChannel, brightness);
  }
}
