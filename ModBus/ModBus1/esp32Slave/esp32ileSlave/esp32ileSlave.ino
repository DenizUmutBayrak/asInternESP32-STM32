#include <WiFi.h>
#include <ModbusTCP.h>
#include <DHT.h> // DHT sensör kütüphanesi 
#include <ArduinoJson.h> // Bu kütüphaneye artık ihtiyacımız yok, kaldırıldı

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

// DHT Sensör Ayarları
#define DHTPIN 15      // DHT11 veri pini GPIO 15'e bağlı
#define DHTTYPE DHT11  // DHT11 kullanıyoruz (DHT22 için DHT22 olarak değiştirin)
DHT dht(DHTPIN, DHTTYPE); // DHT nesnesi oluştur

unsigned long lastDHTReadTime = 0;
const unsigned long DHT_READ_INTERVAL_MS = 2000; // DHT'yi her 2 saniyede bir oku (DHT11 için min 1 saniye)

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\nESP32-S3 Basliyor...");

  // Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2); // STM32 seri haberleşmesi tamamen kaldırıldı

  dht.begin(); // DHT sensörünü başlat

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
  mb.addHreg(0, 0); // Nem için
  mb.addHreg(1, 0); // Sıcaklık için
  mb.addHreg(SECRET_KEY_REGISTER_ADDRESS, 0); // Gizli anahtar için (başlangıç değeri 0)
}

void loop() {
  mb.task(); // Modbus sunucusunun görevlerini işle

  uint16_t currentSecretKeyInRegister = mb.Hreg(SECRET_KEY_REGISTER_ADDRESS);

  // Anahtar doğrulama ve erişim izni mantığı
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

  // DHT11 Sensöründen Veri Okuma
  if (millis() - lastDHTReadTime >= DHT_READ_INTERVAL_MS) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    // Sensör okuma hatalarını kontrol et
    if (isnan(h) || isnan(t)) {
      Serial.println("DHT'den okuma yapilamadi!");
    } else {
      uint16_t humidity = (uint16_t)h; // float'tan uint16_t'ye dönüştür
      uint16_t temperature = (uint16_t)t; // float'tan uint16_t'ye dönüştür

      Serial.print("DHT Nem: ");
      Serial.print(humidity);
      Serial.print("%, DHT Sicaklik: ");
      Serial.print(temperature);
      Serial.println("C");

      // Sadece yetkili erişim varsa Modbus Register'larını güncelle
      if (accessGranted) { 
        mb.Hreg(0, humidity);
        mb.Hreg(1, temperature);
        Serial.println("Modbus register'lari guncellendi (Yetkili Erisim).");
      } else {
        Serial.println("Gecersiz anahtar! Modbus register'lari guncellenmedi.");
      }
    }
    lastDHTReadTime = millis(); // Son okuma zamanını güncelle
  }

  delay(50); // CPU'yu rahatlatmak için kısa bir gecikme
}