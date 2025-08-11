#include <OneWire.h>

#define ONE_WIRE_BUS 18
OneWire oneWire(ONE_WIRE_BUS);

void setup() {
  Serial.begin(115200);
}

void loop() {
  byte data[9];
  
  oneWire.reset();
  oneWire.skip();         // Tek sensör var ise adres atlanır
  oneWire.write(0x44);    // Ölçüm komutu gönder
  
  delay(750);             // Ölçüm süresi bekle

  oneWire.reset();
  oneWire.skip();
  oneWire.write(0xBE);    // Ölçüm verisi oku

  for (int i = 0; i < 9; i++) {
    data[i] = oneWire.read();
  }

  int16_t raw = (data[1] << 8) | data[0];
  float celsius = (float)raw / 16.0;

  Serial.print("Sıcaklık: ");
  Serial.print(celsius, 4);
  Serial.println(" °C");

  delay(1000);
}
