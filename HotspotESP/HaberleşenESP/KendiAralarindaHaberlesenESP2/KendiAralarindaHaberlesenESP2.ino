#include <WiFi.h>
#include <WiFiClientSecure.h> // Güvenli (SSL/TLS) bağlantı için bu kütüphane gerekli!
#include <WiFiAP.h>         // Access Point modu için
#include <WebServer.h>      // HTTP sunucusu için
#include <PubSubClient.h>   // MQTT (HiveMQ) için
#include <ArduinoJson.h>    // Gelen JSON verisini ayrıştırmak için

// --- ESP-2'nin Access Point (AP) Ayarları (ESP-1 buraya bağlanacak) ---
const char* ap_ssid = "MyESP32Server";       // ESP-1'deki 'ssid' ile BİREBİR AYNI OLMALIDIR!
const char* ap_password = "mypassword";      // ESP-1'deki 'password' ile BİREBİR AYNI OLMALIDIR!

// --- ESP-2'nin İstasyon (STA) Ayarları (İnternete bağlanmak için) ---
// LÜTFEN KENDİ EVİNİZİN WI-FI BİLGİLERİYLE GÜNCELLEYİN!
const char* sta_ssid = "DenizUmut";         // Kendi evinizin Wi-Fi adı
const char* sta_password = "12345678";      // Kendi evinizin Wi-Fi şifresi

// --- LED Ayarları ---
#define LED_PIN 16        // Veri alındığında yanacak LED'in bağlı olduğu GPIO

// --- HTTP Sunucusu (ESP-1'den gelen veriler için) ---
WebServer server(80);

// --- MQTT (HiveMQ) Ayarları ---
const char* mqtt_server = "4b74b290196b43e5ac538a4b3b907de2.s1.eu.hivemq.cloud"; // HiveMQ Cloud Broker adresiniz
const int mqtt_port = 8883; // MQTT TCP Portu (SSL/TLS için 8883 kullanılır!)

// HTML web sayfasının dinlediği konularla aynı olmalı!
const char* mqtt_client_id = "ESP32_DataGateway"; // HiveMQ'da benzersiz bir istemci ID'si
const char* mqtt_topic_temp = "esp32/temperature";
const char* mqtt_topic_humidity = "esp32/humidity";
const char* mqtt_username = "webuser"; // HiveMQ'daki web kullanıcı adınız (HTML'dekiyle aynı!)
const char* mqtt_password = "n12345678N"; // HiveMQ'daki web şifreniz (HTML'dekiyle aynı!)

WiFiClientSecure espClient; // Normal WiFiClient yerine WiFiClientSecure kullanıyoruz
PubSubClient mqttClient(espClient);

// --- Fonksiyon Tanımlamaları ---
void handlePostData();
void handleNotFound();
void reconnectMqtt();

void setup() {
  Serial.begin(115200);
  Serial.println();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Başlangıçta LED'i kapat

  // 1. GÖREV BAŞLANGICI: ESP-2'yi Access Point olarak başlat
  Serial.print("Access Point Kuruluyor...");
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress myAPIP = WiFi.softAPIP();
  Serial.print("AP IP Adresi: ");
  Serial.println(myAPIP);

  // 2. GÖREV BAŞLANGICI: ESP-2'yi İstasyon olarak başlat ve internete bağlan
  Serial.print("İnternet Wi-Fi'ye bağlanılıyor: ");
  Serial.println(sta_ssid);
  WiFi.begin(sta_ssid, sta_password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) { // 20 saniye bekle
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("İnternet Wi-Fi'ye Bağlandı!");
    Serial.print("Kendi İnternet IP adresi: ");
    Serial.println(WiFi.localIP());

    // --- Güvenli bağlantı için sertifika doğrulamasını atla ---
    espClient.setInsecure(); // SERTİFİKA DOĞRULAMAYI KAPATIR!
    Serial.println("SSL/TLS Sertifika Doğrulaması Devre Dışı Bırakıldı (Güvenli Değildir!).");

  } else {
    Serial.println("İnternet Wi-Fi bağlantısı başarısız oldu. Lütfen SSID/Şifre'yi kontrol edin.");
    Serial.println("HiveMQ'ya veri gönderilemeyebilir.");
  }

  // HTTP Sunucu Yönlendirmeleri
  server.on("/data", HTTP_POST, handlePostData);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP Sunucu Başlatıldı.");

  // MQTT İstemcisini Ayarla
  mqttClient.setServer(mqtt_server, mqtt_port);
}

void loop() {
  server.handleClient();
  mqttClient.loop();

  // MQTT bağlantısını kontrol et ve gerekirse yeniden bağlan (İnternet bağlantısı varsa)
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    reconnectMqtt();
  }
}

// --- Fonksiyon Uygulamaları ---

void handlePostData() {
  Serial.println("ESP-1'den POST isteği alındı.");
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);

  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    Serial.print("Alınan JSON: ");
    Serial.println(body);

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      Serial.print(F("deserializeJson() başarısız oldu: "));
      Serial.println(error.f_str());
      server.send(500, "text/plain", "JSON ayrıştırma hatası.");
      return;
    }

    float humidity = doc["humidity"];
    float temperature = doc["temperature"];

    Serial.print("Ayrıştırılan Nem: "); Serial.println(humidity);
    Serial.print("Ayrıştırılan Sıcaklık: "); Serial.println(temperature);

    // Veriyi HiveMQ'ya gönder
    if (mqttClient.connected()) {
      String tempString = String(temperature, 1);
      mqttClient.publish(mqtt_topic_temp, tempString.c_str());
      Serial.println("Sıcaklık HiveMQ'ya gönderildi: " + tempString);

      String humString = String(humidity, 1);
      mqttClient.publish(mqtt_topic_humidity, humString.c_str());
      Serial.println("Nem HiveMQ'ya gönderildi: " + humString);

    } else {
      Serial.println("MQTT'ye bağlı değil veya internet yok, HiveMQ'ya gönderilemedi.");
    }

    server.send(200, "text/plain", "Veri başarıyla alındı ve işlendi.");
  } else {
    server.send(400, "text/plain", "Geçersiz istek gövdesi.");
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Bulunamadı");
}

void reconnectMqtt() {
  while (!mqttClient.connected()) {
    Serial.print("MQTT'ye bağlanmaya çalışılıyor...");
    if (mqttClient.connect(mqtt_client_id, mqtt_username, mqtt_password)) {
      Serial.println("bağlandı!");
    } else {
      Serial.print("başarısız, rc=");
      Serial.print(mqttClient.state()); // Hata kodunu göster
      Serial.println(" 5 saniye sonra tekrar denenecek");
      delay(5000);
    }
  }
}