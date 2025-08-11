#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#define RELAY_PIN 15          
#define TEMP_THRESHOLD 27.0   

const char* ssid = "DenizUmut";
const char* password = "12345678";

const char* mqtt_server = "4b74b290196b43e5ac538a4b3b907de2.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "esp32user";
const char* mqtt_password = "n12345678N";

WiFiClientSecure secureClient;
PubSubClient client(secureClient);

bool manualControl = false;
bool manualState = false;
String currentStatus = "Başlangıçta durum yok";
float currentTemperature = 0.0;
String clientId;

String uartBuffer = ""; // UART’dan okunan veri tamponu

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (String(topic) == "fan/control") {
    if (message == "fan_on") {
      manualControl = true;
      manualState = true;
      digitalWrite(RELAY_PIN, LOW);
      client.publish("fan/status", "fan_on");
      Serial2.println("fan_on");
    } else if (message == "fan_off") {
      manualControl = true;
      manualState = false;
      digitalWrite(RELAY_PIN, HIGH);
      client.publish("fan/status", "fan_off");
      Serial2.println("fan_off");
    } else if (message == "auto") {
      manualControl = false;
      // otomatik modda fan kontrolü loop içinde yapılacak
      client.publish("fan/status", "auto");
      Serial2.println("auto");
    }
  }
}

void connectMQTT() {
  clientId = "ESP32Client-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  while (!client.connected()) {
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      client.subscribe("fan/control");
    } else {
      delay(5000);
    }
  }
}

// STM32’den UART üzerinden sıcaklık okuma ve parse etme
void readTemperatureFromUART() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      currentTemperature = uartBuffer.toFloat();
      uartBuffer = "";
    } else if (c != '\r') { // Satır sonu karakterini yoksay
      uartBuffer += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17 (ESP32 pinleri, ayarla)

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Fan kapalı (aktif LOW röle)

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  secureClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  connectMQTT();
}

void loop() {
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  // UART’dan sıcaklık oku
  readTemperatureFromUART();

  // Sıcaklığı MQTT'ye yayınla her saniye
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 1000) {
    lastPublish = millis();
    char tempStr[16];
    snprintf(tempStr, sizeof(tempStr), "%.2f", currentTemperature);
    client.publish("fan/temperature", tempStr);
  }

  // Fan kontrolü
  if (manualControl) {
    digitalWrite(RELAY_PIN, manualState ? LOW : HIGH);
  } else {
    // Otomatik modda sıcaklığa göre fan aç/kapa
    if (currentTemperature >= TEMP_THRESHOLD) {
      digitalWrite(RELAY_PIN, LOW);
      client.publish("fan/status", "fan_on");
    } else {
      digitalWrite(RELAY_PIN, HIGH);
      client.publish("fan/status", "fan_off");
    }
  }

  delay(100);
}
