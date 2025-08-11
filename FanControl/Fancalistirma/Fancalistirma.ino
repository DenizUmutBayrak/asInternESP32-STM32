#include <OneWire.h>

#define ONE_WIRE_BUS 18       // DS18B20 veri pini
#define RELAY_PIN 15          // Röle kontrol pini
#define TEMP_THRESHOLD 27.0   // Fanı açacak sıcaklık eşiği (°C)

OneWire oneWire(ONE_WIRE_BUS);

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // Röle başlangıçta kapalı (fan kapalı)
}

void loop() {
  byte data[9];

  oneWire.reset();
  oneWire.skip();
  oneWire.write(0x44);     // Sıcaklık ölçümünü başlat
  delay(750);              // Ölçüm süresi

  oneWire.reset();
  oneWire.skip();
  oneWire.write(0xBE);     // Ölçüm verisini oku

  for (int i = 0; i < 9; i++) {
    data[i] = oneWire.read();
  }

  int16_t raw = (data[1] << 8) | data[0];
  float celsius = (float)raw / 16.0;

  Serial.print("Sıcaklık: ");
  Serial.print(celsius, 2);
  Serial.print(" °C - Fan durumu: ");

  if (celsius >= TEMP_THRESHOLD) {
    digitalWrite(RELAY_PIN, LOW);  // Röle aktif → fan çalışır
    Serial.println("Çalışıyor");
  } else {
    digitalWrite(RELAY_PIN, HIGH);   // Röle pasif → fan kapalı
    Serial.println("Kapalı");
  }

  delay(1000);  // 1 saniyede bir kontrol
}
