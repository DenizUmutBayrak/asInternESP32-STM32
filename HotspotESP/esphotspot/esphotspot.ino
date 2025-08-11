#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>

#define ONE_WIRE_BUS 18       // DS18B20 veri pini
#define RELAY_PIN 15          // Fan rölesi pin (aktif LOW)
#define TEMP_THRESHOLD 27.0   // Fan açma sıcaklık eşiği (°C)

// Wi-Fi access point bilgileri
const char* ssid = "ESP32_Hotspot";
const char* password = "12345678";

// Statik IP yapılandırması
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

OneWire oneWire(ONE_WIRE_BUS);
WebServer server(80);

bool manualControl = false;  // false = otomatik, true = manuel
bool manualState = false;    // manuel modda fan durumu (true = açık)
float currentTemperature = 0.0;
String currentStatus = "Başlangıçta durum yok";

// Sıcaklık okuma fonksiyonu
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

// Fanı aç/kapat fonksiyonu
void setFan(bool on) {
  if (on) {
    digitalWrite(RELAY_PIN, LOW);  // Röle aktif LOW, fan aç
    currentStatus = manualControl ? "Manuel: FAN AÇIK" : "Otomatik: FAN AÇIK";
  } else {
    digitalWrite(RELAY_PIN, HIGH); // Röle kapalı, fan kapalı
    currentStatus = manualControl ? "Manuel: FAN KAPALI" : "Otomatik: FAN KAPALI";
  }
}

// HTML sayfa oluşturma
String generateHTML() {
  String html = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 400px; margin: auto; text-align: center; }";
  html += "h1 { color: #333; }";
  html += ".button { display: inline-block; width: 80%; padding: 15px; margin: 10px; font-size: 18px; color: white; border: none; border-radius: 5px; }";
  html += ".on { background-color: #4CAF50; }";
  html += ".off { background-color: #f44336; }";
  html += ".refresh { background-color: #2196F3; padding: 10px 20px; margin: 15px; color: white; font-size: 18px; border-radius: 5px; }";
  html += ".footer { margin-top: 20px; color: #666; font-size: 14px; }";
  html += "</style></head><body>";
  html += "<h1>ESP32 Fan Control</h1>";
  html += "<p>Mevcut Sıcaklık: " + String(currentTemperature, 2) + " °C</p>";
  html += "<p>Mod: " + String(manualControl ? "MANUEL" : "OTOMATİK") + "</p>";
  html += String("<p>Fan Durumu: ") + (digitalRead(RELAY_PIN) == LOW ? "AÇIK" : "KAPALI") + "</p>";

  // Manuel mod açma / kapama butonları
  html += "<form action=\"/manual_on\" method=\"GET\">";
  html += "<button class=\"button on\" type=\"submit\">Manuel Fan Aç</button>";
  html += "</form>";

  html += "<form action=\"/manual_off\" method=\"GET\">";
  html += "<button class=\"button off\" type=\"submit\">Manuel Fan Kapat</button>";
  html += "</form>";

  // Otomatik moda geçiş butonu
  html += "<form action=\"/auto\" method=\"GET\">";
  html += "<button class=\"button refresh\" type=\"submit\">Otomatik Mod</button>";
  html += "</form>";

  // Yenile butonu
  html += "<form action=\"/refresh\" method=\"GET\">";
  html += "<button class=\"button refresh\" type=\"submit\">Yenile</button>";
  html += "</form>";

  html += "<div class=\"footer\">Tech StudyCell</div>";
  html += "</body></html>";
  return html;
}

// Server yönlendirme ayarları
void setupServerRoutes() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", generateHTML());
  });

  server.on("/manual_on", HTTP_GET, []() {
    manualControl = true;
    manualState = true;
    setFan(true);
    server.send(200, "text/html", generateHTML());
  });

  server.on("/manual_off", HTTP_GET, []() {
    manualControl = true;
    manualState = false;
    setFan(false);
    server.send(200, "text/html", generateHTML());
  });

  server.on("/auto", HTTP_GET, []() {
    manualControl = false;
    server.send(200, "text/html", generateHTML());
  });

  server.on("/refresh", HTTP_GET, []() {
    server.send(200, "text/html", generateHTML());
  });
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Röle kapalı başlangıç

  // WiFi Access Point kurulum
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);
  Serial.print("Access Point IP: ");
  Serial.println(WiFi.softAPIP());

  setupServerRoutes();
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();

  // Sıcaklık güncelle
  currentTemperature = readTemperature();

  // Otomatik modda fanı sıcaklığa göre aç/kapat
  if (!manualControl) {
    if (currentTemperature >= TEMP_THRESHOLD) {
      setFan(true);
    } else {
      setFan(false);
    }
  }

  delay(500);
}
