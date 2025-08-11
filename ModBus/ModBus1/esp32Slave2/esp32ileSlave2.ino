#include <WiFi.h>
#include <ModbusTCP.h>
#include <OneWire.h>          // DS18B20 için OneWire kütüphanesi
#include <DallasTemperature.h> // DS18B20 için DallasTemperature kütüphanesi

// WiFi Ayarları
const char* ssid = "DenizUmut";       // BURAYA KENDİ WIFI ADINI YAZ
const char* password = "12345678";   // BURAYA KENDİ WIFI ŞİFRENİ YAZ

// Modbus Ayarları
ModbusTCP mb; // Modbus TCP nesnesi

// --- GÜVENLİK AYARLARI ---
const uint16_t SECRET_KEY = 12345;          // Python Master ile AYNI olmalı!
const uint16_t SECRET_KEY_REGISTER_ADDRESS = 99; // Master'ın gizli anahtarı yazacağı Modbus adresi (40100)
unsigned long lastValidAccessTime = 0;      // Son geçerli erişim zamanı (millis() değeri)
const unsigned long ACCESS_TIMEOUT_MS = 20000; // Erişim izni süresi (milisaniye cinsinden), 20 saniye

bool accessGranted = false; // Erişim izni durumunu tutan bayrak

// DS18B20 Sensör Ayarları
#define ONE_WIRE_BUS 15 // DS18B20 veri pini GPIO 15'e bağlı (bu ESP için)
OneWire oneWire(ONE_WIRE_BUS); // OneWire nesnesi oluştur
DallasTemperature sensors(&oneWire); // DallasTemperature nesnesi oluştur

unsigned long lastDS18B20ReadTime = 0;
const unsigned long DS18B20_READ_INTERVAL_MS = 5000; // DS18B20'yi her 5 saniyede bir oku

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\nESP32-S3 Basliyor (DS18B20)...");

  sensors.begin(); // DS18B20 sensörünü başlat

  Serial.print("WiFi'ye baglaniliyor: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi baglandi!");
  Serial.print("IP Adresi: ");
  Serial.println(WiFi.localIP());

  mb.server(); 

  // Modbus Holding Register'larını tanımla ve başlangıç değerlerini ata
  mb.addHreg(0, 0); // DS18B20 Sıcaklık için (Register 0)
  mb.addHreg(SECRET_KEY_REGISTER_ADDRESS, 0); // Gizli anahtar için (başlangıç değeri 0)
}

void loop() {
  mb.task(); // Modbus sunucusunun görevlerini işle

  uint16_t currentSecretKeyInRegister = mb.Hreg(SECRET_KEY_REGISTER_ADDRESS);

  // Güvenlik anahtarı doğrulama mantığı
  if (currentSecretKeyInRegister == SECRET_KEY) {
      if (!accessGranted) {
          Serial.println("Gizli anahtar alindi ve dogrulandi. Erisim verildi.");
          accessGranted = true;
          lastValidAccessTime = millis();
      }
  } else {
      if (currentSecretKeyInRegister != 0) {
          Serial.print("UYARI: Gecersiz gizli anahtar denemesi: ");
          Serial.println(currentSecretKeyInRegister);
      }
      if (accessGranted) {
          Serial.println("Gecersiz anahtar nedeniyle erisim iptal edildi.");
          accessGranted = false;
          lastValidAccessTime = 0;
          mb.Hreg(SECRET_KEY_REGISTER_ADDRESS, 0);
      }
  }

  // Erişim süresi dolduysa erişim iznini sıfırla
  if (accessGranted && (millis() - lastValidAccessTime > ACCESS_TIMEOUT_MS)) {
      Serial.println("Erisim suresi doldu. Anahtar sifirlandi. Erisim iptal edildi.");
      mb.Hreg(SECRET_KEY_REGISTER_ADDRESS, 0);
      accessGranted = false;
      lastValidAccessTime = 0;
  }

  // DS18B20 Sensöründen Veri Okuma
  if (millis() - lastDS18B20ReadTime >= DS18B20_READ_INTERVAL_MS) {
    sensors.requestTemperatures(); 
    float ds18b20_temp_c = sensors.getTempCByIndex(0); 

    if (ds18b20_temp_c == DEVICE_DISCONNECTED_C) {
      Serial.println("DS18B20 okuma yapilamadi veya baglanti koptu!");
    } else {
      uint16_t ds18b20_temperature = (uint16_t)ds18b20_temp_c; 

      Serial.print("DS18B20 Sicaklik: ");
      Serial.print(ds18b20_temperature);
      Serial.println("C");

      // Sadece yetkili erişim varsa Modbus Register'larını güncelle
      if (accessGranted) { 
        mb.Hreg(0, ds18b20_temperature); // DS18B20 sıcaklığını Hreg(0)'a yaz
        Serial.println("Modbus register 0 guncellendi (Yetkili Erisim).");
      } else {
        Serial.println("Gecersiz anahtar! Modbus register 0 guncellenmedi.");
      }
    }
    lastDS18B20ReadTime = millis();
  }

  delay(50); 
}