#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <OneWire.h>

#define ONE_WIRE_BUS 18       // DS18B20 veri pini
#define RELAY_PIN 15          // Röle kontrol pini
#define TEMP_THRESHOLD 27.0   // Fan açma sıcaklık eşiği (°C)

const char* ssid = "DenizUmut";
const char* password = "12345678";

const char* mqtt_server = "4b74b290196b43e5ac538a4b3b907de2.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;  // TLS portu
const char* mqtt_user = "esp32user";
const char* mqtt_password = "n12345678N";

WiFiClientSecure secureClient;
PubSubClient client(secureClient);

OneWire oneWire(ONE_WIRE_BUS);

bool manualControl = false;
bool manualState = false;
String currentStatus = "Başlangıçta durum yok";
float currentTemperature = 0.0;
String clientId;  // Benzersiz clientId için global değişken

void showMenu() {
  Serial.println("\n------ MENÜ ------");
  Serial.println("1 - Fan Aç (MANUEL ON)");
  Serial.println("2 - Fan Kapat (MANUEL OFF)");
  Serial.println("3 - Otomatik Sıcaklık Kontrolü (AUTO)");
  Serial.println("4 - Mevcut Sıcaklığı Göster");
  Serial.println("--------------------");
  Serial.println("Güncel Durum: " + currentStatus);
  Serial.print("Güncel Sıcaklık: ");
  Serial.print(currentTemperature, 2);
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
  float celsius = raw / 16.0;
  return celsius;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mesaj geldi [");
  Serial.print(topic);
  Serial.print("]: ");

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  if (String(topic) == "fan/control") {
    if (message == "fan_on") {
      manualControl = true;
      manualState = true;
      currentStatus = "Manuel: FAN AÇIK (MQTT)";
      Serial.println("Manuel kontrol: FAN AÇIK (MQTT)");
      digitalWrite(RELAY_PIN, LOW);
      client.publish("fan/status", "fan_on");
    } 
    else if (message == "fan_off") {
      manualControl = true;
      manualState = false;
      currentStatus = "Manuel: FAN KAPALI (MQTT)";
      Serial.println("Manuel kontrol: FAN KAPALI (MQTT)");
      digitalWrite(RELAY_PIN, HIGH);
      client.publish("fan/status", "fan_off");
    }
    else if (message == "auto") {
      manualControl = false;
      currentStatus = "Otomatik sıcaklık kontrolüne geçti (MQTT)";
      Serial.println("Otomatik moda geçildi (MQTT)");
    }
  }
}

void connectMQTT() {
  clientId = "ESP32Client-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  while (!client.connected()) {
    Serial.print("MQTT broker'a bağlanılıyor...");
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("Bağlandı!");
      client.subscribe("fan/control");
      // İlk durum mesajı gönder
      String statusMsg = manualControl ? (manualState ? "fan_on" : "fan_off") : (currentTemperature >= TEMP_THRESHOLD ? "fan_on" : "fan_off");
      client.publish("fan/status", statusMsg.c_str());
    } else {
      Serial.print("Bağlantı başarısız, rc=");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Başlangıçta fan kapalı (aktif LOW)

  Serial.print("WiFi'ye bağlanılıyor...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi bağlantısı başarılı!");
  Serial.print("IP Adresi: ");
  Serial.println(WiFi.localIP());

  secureClient.setInsecure();  // Sertifika kontrolünü atla

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  currentTemperature = readTemperature();
  showMenu();

  connectMQTT();
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
      digitalWrite(RELAY_PIN, LOW);
      client.publish("fan/status", "fan_on");
    }
    else if (input == "2") {
      manualControl = true;
      manualState = false;
      currentStatus = "Manuel: FAN KAPALI";
      Serial.println("Manuel kontrol: FAN KAPALI");
      digitalWrite(RELAY_PIN, HIGH);
      client.publish("fan/status", "fan_off");
    }
    else if (input == "3") {
      manualControl = false;
      currentStatus = "Otomatik sıcaklık kontrolünde";
      Serial.println("Otomatik sıcaklık kontrolüne geçildi.");
      client.publish("fan/status", "auto");
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
    showMenu();
  }

  if (!client.connected()) {
    Serial.println("MQTT bağlantısı kesildi, yeniden bağlanıyor...");
    connectMQTT();
  }
  client.loop();

  currentTemperature = readTemperature();

  if (manualControl) {
    if (manualState) {
      digitalWrite(RELAY_PIN, LOW);
    } else {
      digitalWrite(RELAY_PIN, HIGH);
    }
    delay(100);
    return;
  }

  // Otomatik modda sıcaklık kontrolü
  if (currentTemperature >= TEMP_THRESHOLD) {
    digitalWrite(RELAY_PIN, LOW);
    currentStatus = "Otomatik: FAN ÇALIŞIYOR";
  }
  else {
    digitalWrite(RELAY_PIN, HIGH);
    currentStatus = "Otomatik: FAN DURDU";
  }

  delay(1000);
}  