#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// --- Sabitler (Constants) ---
#define RELAY_PIN 15            // Fanın bağlı olduğu röle pini
#define TEMP_THRESHOLD 27.0     // Fanın otomatik olarak açılacağı sıcaklık eşiği

// WiFi Bilgileri
const char* ssid = "DenizUmut";
const char* password = "12345678";

// MQTT Bilgileri
const char* mqtt_server = "4b74b290196b43e5ac538a4b3b907de2.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "esp32user";
const char* mqtt_password = "n12345678N";

// --- Global Değişkenler (Global Variables) ---
WiFiClientSecure secureClient;
PubSubClient client(secureClient);

bool manualControl = false;    // Manuel modda mı? (true: Manuel, false: Otomatik)
bool manualState = false;      // Manuel moddayken fanın durumu (true: Açık, false: Kapalı)
float currentTemperature = NAN; // STM32'den gelen anlık sıcaklık değeri

String lastFanStatusForSTM32 = ""; // STM32'ye en son gönderilen fan durumu

// --- Fonksiyon Tanımlamaları (Function Prototypes) ---
void connectWiFi();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleSerialCommand(String cmd);
void processTemperatureData(String &data);
void readTemperatureFromUART();
void printMenu();
void controlFan();
void sendFanStatusToSTM32(); // Yeni fonksiyon prototipi

// --- Setup Fonksiyonu (Setup Function) ---
void setup() {
  Serial.begin(115200);
  // ESP32'nin UART2 pinlerini STM32'nin UART2 pinlerine (PA2/PA3) bağlamayı unutmayın.
  // RX: GPIO16 (STM32 TX'ine bağlı olacak), TX: GPIO17 (STM32 RX'ine bağlı olacak)
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Başlangıçta fan kapalı (HIGH röleyi kapatıyorsa)

  connectWiFi();

  secureClient.setInsecure(); // Geliştirme için; üretimde sertifika kullanmayın!
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  connectMQTT();

  Serial.println("=== Sistem Hazır ===");
  Serial.println("Komutlar: 1-FAN AÇ | 2-FAN KAPAT | 3-OTOMATİK");
}

// --- Loop Fonksiyonu (Loop Function) ---
unsigned long lastMenuTime = 0;
unsigned long lastPublish = 0; // Hem MQTT hem de STM32'ye veri göndermek için kullanılır

void loop() {
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  readTemperatureFromUART(); // STM32'den sıcaklık verisini okur (DS18B20'den)
  controlFan();              // Fanı kontrol eder (manuel veya otomatik)
  sendFanStatusToSTM32();    // STM32'ye fan durumunu gönderir

  // MQTT'ye sıcaklık gönder (her 1 saniyede bir)
  if (millis() - lastPublish > 1000) {
    lastPublish = millis();
    char tempStr[16];
    if (isnan(currentTemperature)) {
      strcpy(tempStr, "Sensor Error");
    } else {
      snprintf(tempStr, sizeof(tempStr), "%.2f", currentTemperature);
    }
    client.publish("fan/temperature", tempStr);
  }

  // Menüyü belirli aralıklarla yazdır
  if (millis() - lastMenuTime > 2000) {
    lastMenuTime = millis();
    printMenu();
  }

  // Seri porttan gelen komutları oku
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    handleSerialCommand(command);
  }

  delay(50); // Kısa bir gecikme
}

// --- Fonksiyon Uygulamaları (Function Implementations) ---

void connectWiFi() {
  Serial.print("[WiFi] Bağlanılıyor");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Bağlandı!");
}

void connectMQTT() {
  String clientId = "ESP32Client-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  while (!client.connected()) {
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      client.subscribe("fan/control");
      Serial.println("[MQTT] Bağlantı başarılı, 'fan/control' konusuna abone olundu.");
    } else {
      Serial.print("[MQTT] Bağlantı başarısız, rc=");
      Serial.print(client.state());
      Serial.println(", 5sn sonra tekrar denenecek.");
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  if (String(topic) == "fan/control") {
    handleSerialCommand(message);
  }
}

void handleSerialCommand(String cmd) {
  cmd.trim();
  if (cmd == "1" || cmd == "fan_on") {
    manualControl = true;
    manualState = true;
    Serial.println("[SERIAL] Fan manuel olarak açıldı.");
    client.publish("fan/status", "manuel_on"); // MQTT'ye bildir
  } else if (cmd == "2" || cmd == "fan_off") {
    manualControl = true;
    manualState = false;
    Serial.println("[SERIAL] Fan manuel olarak kapatıldı.");
    client.publish("fan/status", "manuel_off"); // MQTT'ye bildir
  } else if (cmd == "3" || cmd == "auto") {
    manualControl = false;
    Serial.println("[SERIAL] Otomatik moda geçildi.");
    client.publish("fan/status", "auto_mode"); // MQTT'ye bildir
  } else {
    Serial.println("[SERIAL] Geçersiz komut!");
  }
}

void processTemperatureData(String &data) {
  data.trim();

  // UART'tan gelen ham veriyi görmek için (debug amaçlı)
  // Serial.printf("[UART RAW] '%s'\n", data.c_str());

  if (data.indexOf("ERROR") >= 0 || data.length() == 0) {
    currentTemperature = NAN;
    Serial.println("[UART] Hatalı sensör verisi alındı.");
    return;
  }

  long tempInt = data.toInt();
  if (tempInt == 0 && data != "0") { // 0 dışında bir değer 0'a dönüştüyse geçersizdir
    currentTemperature = NAN;
    Serial.println("[UART] Geçersiz sayı formatı.");
    return;
  }

  currentTemperature = tempInt / 100.0f;
  Serial.printf("[UART] Başarılı sıcaklık okuma: %.2f°C\n", currentTemperature);
}

void readTemperatureFromUART() {
  static String tempBuffer;

  while (Serial2.available()) {
    char c = Serial2.read();

    if (c == '\n') {
      processTemperatureData(tempBuffer);
      tempBuffer = ""; // Buffer'ı sıfırla
    } else if (c != '\r') { // CR karakterini (Windows için \r\n) yok say
      tempBuffer += c;
    }
  }
}

void printMenu() {
  Serial.println();
  Serial.println("=== MENU ===");
  Serial.println("1 - FAN AÇ");
  Serial.println("2 - FAN KAPAT");
  Serial.println("3 - OTOMATİK MOD");
  Serial.print("Mevcut Sıcaklık: ");
  if (isnan(currentTemperature)) {
    Serial.print("Sensor Hatası");
  } else {
    Serial.print(currentTemperature);
    Serial.print(" °C");
  }
  Serial.print(" | Mod: ");
  if (manualControl) {
    Serial.println(manualState ? "Manuel - FAN AÇIK" : "Manuel - FAN KAPALI");
  } else {
    Serial.println("Otomatik");
    // Otomatik moddayken fanın gerçek durumunu da göster
    if (!isnan(currentTemperature)) {
      if (currentTemperature >= TEMP_THRESHOLD) {
        Serial.println(" (FAN ŞU AN AÇIK)");
      } else {
        Serial.println(" (FAN ŞU AN KAPALI)");
      }
    }
  }
  Serial.println("Seçiminizi girin ve ENTER'a basın:");
}

void controlFan() {
  if (manualControl) {
    digitalWrite(RELAY_PIN, manualState ? LOW : HIGH); // Manuel kontrol
  } else {
    // Otomatik kontrol
    if (!isnan(currentTemperature) && currentTemperature >= TEMP_THRESHOLD) {
      digitalWrite(RELAY_PIN, LOW); // Fanı aç
    } else {
      digitalWrite(RELAY_PIN, HIGH); // Fanı kapat
    }
  }
}

void sendFanStatusToSTM32() {
  String currentFanStatusForSTM32;

  if (manualControl) {
    if (manualState) {
      currentFanStatusForSTM32 = "FAN:ON"; // Manuel açık
    } else {
      currentFanStatusForSTM32 = "FAN:OFF"; // Manuel kapalı
    }
  } else { // Otomatik kontrol
    if (!isnan(currentTemperature) && currentTemperature >= TEMP_THRESHOLD) {
      currentFanStatusForSTM32 = "FAN:AUTO_ON"; // Otomatik açık
    } else {
      currentFanStatusForSTM32 = "FAN:AUTO_OFF"; // Otomatik kapalı
    }
  }

  // Sadece durum değiştiğinde göndererek gereksiz tekrarları önle
  if (currentFanStatusForSTM32 != lastFanStatusForSTM32) {
    Serial2.print(currentFanStatusForSTM32);
    Serial2.print("\n"); // Yeni satır karakteri ile bitir
    Serial.printf("[UART TX to STM32] %s\n", currentFanStatusForSTM32.c_str());
    lastFanStatusForSTM32 = currentFanStatusForSTM32; // Son gönderilen durumu güncelle
  }
}