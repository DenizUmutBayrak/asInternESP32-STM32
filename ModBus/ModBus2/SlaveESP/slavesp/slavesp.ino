#include <WiFi.h>
#include <ModbusTCP.h>   // BURAYI DEĞİŞTİRDİK! modbus-esp8266 kütüphanesinin doğru TCP başlık dosyası

#include <ArduinoJson.h> // Gelen stringi ayrıştırmak için (yüklediğin kütüphane)

// WiFi Ayarları
const char* ssid = "DenizUmut";        // BURAYA KENDİ WIFI ADINI YAZ
const char* password = "12345678";  // BURAYA KENDİ WIFI ŞİFRENİ YAZ

// Modbus Ayarları
ModbusTCP mb; // Modbus TCP nesnesi (ModbusIP yerine ModbusTCP kullandık)

// Modbus Holding Register'ları
// Modbus adresleri 0 ve 1 olarak tanımlanacak.
// 40001 (Modbus adresi 0): Nem
// 40002 (Modbus adresi 1): Sıcaklık
// Register'lar uint16_t tipindedir (0-65535 arası değer alabilir).
uint16_t modbusRegisters[2]; 

// Seri haberleşme için (STM32 ile haberleşme)
// ESP32-S3'te varsayılan UART2 pinleri GPIO16 (RX) ve GPIO17 (TX) olarak kabul edelim.
// Bu pinleri STM32'den gelen UART2 TX (PA2) ve RX (PA3) pinlerine bağlayacaksın.
// UNUTMA: TX -> RX ve RX -> TX bağlanır.
// STM32 UART2 TX (PA2) -> ESP32-S3 RX (GPIO16)
// STM32 UART2 RX (PA3) -> ESP32-S3 TX (GPIO17)
#define RXD2 16 // ESP32-S3'ün GPIO16'sı RX olarak kullanılacak (STM32'nin TX'ine bağlanacak)
#define TXD2 17 // ESP32-S3'ün GPIO17'si TX olarak kullanılacak (STM32'nin RX'ine bağlanacak)

void setup() {
  // Seri Monitör başlat (Debug ve bilgi için)
  Serial.begin(115200);
  while (!Serial); // Seri Monitör bağlanana kadar bekle (opsiyonel)
  Serial.println("\nESP32-S3 Basliyor...");

  // STM32 ile haberleşecek UART2'yi başlat
  // Baud rate STM32'deki ile aynı olmalı (115200)
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2); 

  // WiFi'ye bağlanma süreci
  Serial.print("WiFi'ye baglaniliyor: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // WiFi bağlantısı kurulana kadar bekle
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi baglandi!");
  Serial.print("IP Adresi: ");
  Serial.println(WiFi.localIP()); // ESP32'nin aldığı IP adresini göster

  // Modbus TCP/IP sunucusunu başlat
  // Bu, ESP32'nin Modbus Master'lardan gelen isteklere yanıt vermesini sağlar.
  mb.server(); 

  // Modbus Holding Register'larını başlangıç değerleriyle tanımla/doldur
  // Modbus adresi 0 -> Holding Register 40001
  mb.addHreg(0, 0); // Başlangıçta nemi 0 olarak ayarla
  // Modbus adresi 1 -> Holding Register 40002
  mb.addHreg(1, 0); // Başlangıçta sıcaklığı 0 olarak ayarla
}

void loop() {
  // Her loop döngüsünde Modbus sunucusunun görevlerini işle
  // Bu, Modbus isteklerini dinler ve yanıtlar.
  mb.task();

  // STM32'den seri port üzerinden veri gelip gelmediğini kontrol et
  if (Serial2.available()) {
    String receivedString = Serial2.readStringUntil('\n'); // Yeni satır karakterine ('\n') kadar oku

    // Gelen veriyi ayrıştır (örnek format: "H:55,T:27")
    int h_start_index = receivedString.indexOf("H:");
    int t_start_index = receivedString.indexOf("T:");
    int comma_index = receivedString.indexOf(",");

    // Eğer gerekli tüm kısımlar bulunuyorsa ayrıştırma yap
    if (h_start_index != -1 && t_start_index != -1 && comma_index != -1) {
      String humidityStr = receivedString.substring(h_start_index + 2, comma_index);
      String temperatureStr = receivedString.substring(t_start_index + 2);

      // String'leri tam sayıya dönüştür
      int incomingHumidity = humidityStr.toInt();
      int incomingTemperature = temperatureStr.toInt();

      // Seri Monitöre gelen verileri yazdır (kontrol için)
      Serial.print("Gelen Nem: ");
      Serial.print(incomingHumidity);
      Serial.print("%, Gelen Sicaklik: ");
      Serial.print(incomingTemperature);
      Serial.println("C");

      // Modbus Holding Register'larını yeni değerlerle güncelle
      // Modbus register'ları 16 bitlik tam sayılar içindir, DHT11'den gelen tam sayılar doğrudan atanabilir.
      mb.Hreg(0, incomingHumidity);   // Modbus Register 40001 (adres 0) -> Nem
      mb.Hreg(1, incomingTemperature); // Modbus Register 40002 (adres 1) -> Sıcaklık
    } else {
      Serial.print("Gelen veri formati hatali veya eksik: ");
      Serial.println(receivedString); // Hatalı formatı seri monitöre yazdır
    }
  }

  delay(50); // CPU'yu rahatlatmak için kısa bir gecikme
}