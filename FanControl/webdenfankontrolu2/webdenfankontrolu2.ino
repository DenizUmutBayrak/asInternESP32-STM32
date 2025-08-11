#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

const char* ssid = "DenizUmut";
const char* password = "12345678";

const char* mqtt_server = "4b74b290196b43e5ac538a4b3b907de2.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "esp32user";
const char* mqtt_password = "n12345678N";

WiFiClientSecure secureClient;
PubSubClient client(secureClient);

String clientId;

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mesaj geldi [");
  Serial.print(topic);
  Serial.print("]: ");
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);
  // İstersen buraya mesaj işleme ekle
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.print("MQTT broker'a bağlanılıyor...");
    // cleanSession = true yapıldı, clientId benzersiz
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password, NULL, 0, true, NULL, false)) {
      Serial.println("Bağlandı!");
      client.subscribe("fan/control");  // Mesajları dinlemek için
    } else {
      Serial.print("Bağlantı başarısız, rc=");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("WiFi bağlanıyor");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi bağlı!");
  Serial.print("IP Adresi: ");
  Serial.println(WiFi.localIP());

  secureClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // MAC adresinden benzersiz clientId üret
  clientId = "ESP32Client-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("Client ID: ");
  Serial.println(clientId);

  connectMQTT();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi bağlantısı kesildi, yeniden bağlanıyor...");
    WiFi.reconnect();
    delay(2000);
  }

  if (!client.connected()) {
    Serial.println("MQTT bağlantısı kesildi, yeniden bağlanıyor...");
    connectMQTT();
  }

  client.loop();
}
