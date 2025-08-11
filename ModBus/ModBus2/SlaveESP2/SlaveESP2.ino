#include <WiFi.h>
#include <ModbusTCP.h>
#include <ArduinoJson.h>

// WiFi Ayarları
const char* ssid = "DenizUmut";       // BURAYA KENDİ WIFI ADINI YAZ
const char* password = "12345678";   // BURAYA KENDİ WIFI ŞİFRENİ YAZ

// Modbus Ayarları
ModbusTCP mb; // Modbus TCP nesnesi

// --- GÜVENLİK AYARLARI ---
const uint16_t SECRET_KEY = 12345;          // Python Master ile AYNI olmalı!
const uint16_t SECRET_KEY_REGISTER_ADDRESS = 99; // Master'ın gizli anahtarı yazacağı Modbus adresi (40100)
unsigned long lastValidAccessTime = 0;      // Son geçerli erişim zamanı (millis() değeri)
const unsigned long ACCESS_TIMEOUT_MS = 20000; // Erişim izni süresi (milisaniye cinsinden), 20 saniye olarak ayarlı

bool accessGranted = false; // Erişim izni durumunu tutan bayrak

// Seri haberleşme için (STM32 ile haberleşme)
#define RXD2 16 // ESP32-S3'ün GPIO16'sı RX olarak kullanılacak (STM32'nin TX'ine bağlanacak)
#define TXD2 17 // ESP32-S3'ün GPIO16'sı TX olarak kullanılacak (STM32'nin RX'ine bağlanacak)

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\nESP32-S3 Basliyor...");

  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2); 

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

  // Eğer register'daki anahtar doğruysa
  if (currentSecretKeyInRegister == SECRET_KEY) {
      if (!accessGranted) { // Eğer erişim daha önce verilmediyse (yani yeni bir doğru anahtar alındıysa)
          Serial.println("Gizli anahtar alindi ve dogrulandi. Erisim verildi.");
          accessGranted = true;
          lastValidAccessTime = millis(); // Erişim verildiğinde zamanı kaydet
      }
  }
  // Eğer register'daki anahtar yanlışsa (veya 0 değilse, yani bir deneme varsa)
  else { // currentSecretKeyInRegister != SECRET_KEY
      if (currentSecretKeyInRegister != 0) { // Sadece 0'dan farklı ve yanlış bir değer geldiğinde uyarı ver
          Serial.print("UYARI: Gecersiz gizli anahtar denemesi: ");
          Serial.println(currentSecretKeyInRegister);
      }
      if (accessGranted) { // Eğer erişim daha önce verilmişken anahtar yanlış olduysa
          Serial.println("Gecersiz anahtar nedeniyle erisim iptal edildi.");
          accessGranted = false;
          lastValidAccessTime = 0; // Erişimi iptal edince zamanı sıfırla
          mb.Hreg(SECRET_KEY_REGISTER_ADDRESS, 0); // Register'ı sıfırla
      }
  }

  // Geçerli erişim süresi dolduysa erişim iznini sıfırla
  if (accessGranted && (millis() - lastValidAccessTime > ACCESS_TIMEOUT_MS)) {
      Serial.println("Erisim suresi doldu. Anahtar sifirlandi. Erisim iptal edildi.");
      mb.Hreg(SECRET_KEY_REGISTER_ADDRESS, 0); // Anahtar register'ını sıfırla
      accessGranted = false; // Erişimi iptal et
      lastValidAccessTime = 0; // Geçerli erişim zamanını sıfırla
  }

  // STM32'den seri port üzerinden veri gelip gelmediğini kontrol et
  if (Serial2.available()) {
    String receivedString = Serial2.readStringUntil('\n'); 
    receivedString.trim();

    // Serial.print("ESP32 Alinan Ham Veri: "); // Bu satır kaldırıldı.
    // Serial.println(receivedString);          // Bu satır kaldırıldı.

    int h_start_index = receivedString.indexOf("H:");
    int t_start_index = receivedString.indexOf("T:");
    int comma_index = receivedString.indexOf(",");

    if (h_start_index != -1 && t_start_index != -1 && comma_index != -1) {
      String humidityStr = receivedString.substring(h_start_index + 2, comma_index);
      String temperatureStr = receivedString.substring(t_start_index + 2);

      uint16_t incomingHumidity = humidityStr.toInt();
      uint16_t incomingTemperature = temperatureStr.toInt();

      Serial.print("Gelen Nem: ");
      Serial.print(incomingHumidity);
      Serial.print("%, Gelen Sicaklik: ");
      Serial.print(incomingTemperature);
      Serial.println("C");

      // Sadece yetkili erişim varsa Modbus Register'larını güncelle
      if (accessGranted) { 
        mb.Hreg(0, incomingHumidity);
        mb.Hreg(1, incomingTemperature);
        Serial.println("Modbus register'lari guncellendi (Yetkili Erisim).");
      } else {
        Serial.println("Gecersiz anahtar! Modbus register'lari guncellenmedi.");
      }

    } else {
      Serial.print("Gelen veri formati hatali veya eksik: ");
      Serial.println(receivedString);
    }
  }

  delay(50); // CPU'yu rahatlatmak için kısa bir gecikme
}