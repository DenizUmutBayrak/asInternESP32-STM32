#include <OneWire.h>

#define ONE_WIRE_BUS 18       // DS18B20 veri pini
#define RELAY_PIN 15          // Röle kontrol pini
#define TEMP_THRESHOLD 27.0   // Fan açma sıcaklık eşiği (°C)

OneWire oneWire(ONE_WIRE_BUS);

bool manualControl = false;
bool manualState = false;
String currentStatus = "Başlangıçta durum yok";  // Güncel durum bilgisini tutar
float currentTemperature = 0.0;                   // Güncel sıcaklık değeri

void showMenu() {
  Serial.println("\n------ MENÜ ------");
  Serial.println("1 - Fan Aç (MANUEL ON)");
  Serial.println("2 - Fan Kapat (MANUEL OFF)");
  Serial.println("3 - Otomatik Sıcaklık Kontrolü (AUTO)");
  Serial.println("4 - Mevcut Sıcaklığı Göster");
  Serial.println("--------------------");
  Serial.println("Güncel Durum: " + currentStatus);
  Serial.print  ("Güncel Sıcaklık: ");
  Serial.print  (currentTemperature, 2);
  Serial.println(" °C");
  Serial.println("Seçiminizi yazıp Enter'a basınız:");
}

float readTemperature() {
  byte data[9];
  oneWire.reset();
  oneWire.skip();
  oneWire.write(0x44);     // Ölçüm başlat
  delay(750);
  oneWire.reset();
  oneWire.skip();
  oneWire.write(0xBE);     // Ölçüm oku
  for (int i = 0; i < 9; i++) {
    data[i] = oneWire.read();
  }
  int16_t raw = (data[1] << 8) | data[0];
  float celsius = (float)raw / 16.0;
  return celsius;
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Röle kapalı (aktif LOW)
  currentTemperature = readTemperature();
  showMenu();                    // Menü setup'ta ilk gösterim
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input == "1") {
      manualControl = true;
      manualState = true;
      currentStatus = "Manuel: FAN AÇIK";
      Serial.println("Manuel kontrol: FAN AÇIK");
    }
    else if (input == "2") {
      manualControl = true;
      manualState = false;
      currentStatus = "Manuel: FAN KAPALI";
      Serial.println("Manuel kontrol: FAN KAPALI");
    }
    else if (input == "3") {
      manualControl = false;
      currentStatus = "Otomatik sıcaklık kontrolünde";
      Serial.println("Otomatik sıcaklık kontrolüne geçildi.");
    }
    else if (input == "4") {
      float tempNow = readTemperature();
      Serial.print("Mevcut Sıcaklık: ");
      Serial.print(tempNow, 2);
      Serial.println(" °C");
    }
    else {
      Serial.println("Geçersiz seçim, tekrar deneyin.");
    }
    // Menü tekrar yazılır, böylece güncel durum ve sıcaklık görünür
    showMenu();
  }

  // Güncel sıcaklığı sürekli güncelle
  currentTemperature = readTemperature();

  if (manualControl) {
    if (manualState) {
      digitalWrite(RELAY_PIN, LOW);  // Röle aktif, fan çalışıyor (aktif LOW)
    }
    else {
      digitalWrite(RELAY_PIN, HIGH); // Röle kapalı, fan duruyor
    }
    delay(100);
    return;
  }

  // Otomatik sıcaklık kontrolü
  if (currentTemperature >= TEMP_THRESHOLD) {
    digitalWrite(RELAY_PIN, LOW);  // Fan aç
    currentStatus = "Otomatik: FAN ÇALIŞIYOR";
  }
  else {
    digitalWrite(RELAY_PIN, HIGH); // Fan kapat
    currentStatus = "Otomatik: FAN DURDU";
  }

  delay(1000);
}
