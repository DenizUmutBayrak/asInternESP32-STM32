#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <PubSubClient.h>
#include <Preferences.h> 
#include <esp_task_wdt.h> // Watchdog Timer kütüphanesi

// Preferences nesnesi
Preferences preferences;

// Wi-Fi Bilgileri
const char* ssid = "DenizUmut";         
const char* password = "12345678";     

// MQTT Broker Bilgileri (HiveMQ Cloud)
const char* mqtt_server = "4b74b290196b43e5ac538a4b3b907de2.s1.eu.hivemq.cloud"; 
const int mqtt_port = 8883; 

// MQTT Kimlik Bilgileri için char dizileri (kaydedilen değerler buraya okunacak)
char defaultMqttUsername[] = "webuser"; 
char defaultMqttPassword[] = "n12345678N"; 

char currentMqttUsername[32]; 
char currentMqttPassword[32]; 

const char* mqtt_client_id = "ESP32S3Client_LightEarthquake"; 

// MQTT Konuları
const char* LIGHT_STATUS_TOPIC = "home/sensor/light_status";
const char* EARTHQUAKE_ALERT_TOPIC = "home/sensor/earthquake_alert";

// Sensör ve Harici LED Pinleri
const int trctPin = 16;       
const int vibrationPin = 15;  
const int externalLedPin = 17; 

// Flash'a yazma tetikleyici pini (GPIO 4, bir butona veya jumper ile GND'ye bağlanacak)
const int SAVE_SETTINGS_PIN = 4; 
const unsigned long SAVE_DELAY_MS = 3000; // Ayarları kaydetmek için butonu basılı tutma süresi (ms)

// Sensör Durumları ve Kontrol Değişkenleri
bool lightOn = false;
bool earthquakeAlert = false;
unsigned long lastVibrationTime = 0;
int vibrationCount = 0;
const int VIBRATION_THRESHOLD = 5;        
const unsigned long VIBRATION_WINDOW = 500; 

WiFiClientSecure espClient; 
PubSubClient client(espClient);

// Watchdog Zamanlayıcı Tanımlaması
const int WATCHDOG_TIMEOUT_SEC = 10; // Saniye cinsinden zaman aşımı süresi (10 saniye)

// --- Wi-Fi Bağlantısı ---
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    yield(); 
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// --- MQTT Gelen Mesajlar ---
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// --- MQTT Yeniden Bağlantı ---
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_client_id, currentMqttUsername, currentMqttPassword)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state()); 
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
    yield(); 
  }
}

// --- MQTT Kimlik Bilgilerini Flash'a Kaydetme Fonksiyonu ---
void saveMqttCredentials() {
  preferences.begin("mqtt_creds", false); 

  preferences.putString("username", defaultMqttUsername);
  preferences.putString("password", defaultMqttPassword);
  
  strncpy(currentMqttUsername, defaultMqttUsername, sizeof(currentMqttUsername) - 1);
  currentMqttUsername[sizeof(currentMqttUsername) - 1] = '\0'; 
  strncpy(currentMqttPassword, defaultMqttPassword, sizeof(currentMqttPassword) - 1);
  currentMqttPassword[sizeof(currentMqttPassword) - 1] = '\0'; 

  Serial.println("MQTT Kimlik Bilgileri Flash'a kaydedildi!");
  preferences.end(); 
}

void setup() {
  Serial.begin(115200);

  pinMode(trctPin, INPUT);
  pinMode(vibrationPin, INPUT);
  pinMode(externalLedPin, OUTPUT); 
  digitalWrite(externalLedPin, LOW);

  pinMode(SAVE_SETTINGS_PIN, INPUT_PULLUP); 

  // --- Watchdog Zamanlayıcıyı başlatma ---
  // esp_task_wdt_config_t yapısı ve init fonksiyonu güncellendi.
  // Ana döngünün çalıştığı çekirdek için watchdog etkinleştirilir.
  // Çekirdek 0 (APP_CPU) için `xPortGetCoreID()` kullanılabilir, veya doğrudan 0 yazılabilir.
  // CONFIG_FREERTOS_COREID_0 gibi makrolar bazen Arduino IDE'de doğrudan erişilebilir olmayabilir.
  
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000, 
    .idle_core_mask = (1 << 0), // Çekirdek 0'ı (APP_CPU) izle. xPortGetCoreID() de kullanılabilir.
    .trigger_panic = false 
  };
  
  esp_task_wdt_init(&wdt_config); 
  esp_task_wdt_add(NULL); // Mevcut görevi (loop) izle
  Serial.print("Watchdog Timer Started with "); Serial.print(WATCHDOG_TIMEOUT_SEC); Serial.println(" seconds timeout.");

  // --- Flash Bellek Preferences'ı başlatma ---
  preferences.begin("mqtt_creds", true); 
  
  size_t usernameLen = preferences.getString("username").length();
  size_t passwordLen = preferences.getString("password").length();

  if (usernameLen == 0 || passwordLen == 0) {
    Serial.println("Flash'ta MQTT kimlik bilgileri bulunamadı veya boş. Varsayılanlar kullanılıyor.");
    strncpy(currentMqttUsername, defaultMqttUsername, sizeof(currentMqttUsername) - 1);
    currentMqttUsername[sizeof(currentMqttUsername) - 1] = '\0';
    strncpy(currentMqttPassword, defaultMqttPassword, sizeof(currentMqttPassword) - 1);
    currentMqttPassword[sizeof(currentMqttPassword) - 1] = '\0';
  } else {
    preferences.getString("username").toCharArray(currentMqttUsername, sizeof(currentMqttUsername));
    preferences.getString("password").toCharArray(currentMqttPassword, sizeof(currentMqttPassword));
    Serial.println("MQTT Kimlik Bilgileri Flash'tan yüklendi.");
  }
  preferences.end(); 

  Serial.print("Current MQTT Username: "); Serial.println(currentMqttUsername);
  Serial.print("Current MQTT Password: "); Serial.println(currentMqttPassword);

  setup_wifi();

  espClient.setInsecure(); 
  Serial.println("SSL/TLS Sertifika Doğrulaması Devre Dışı Bırakıldı (Güvenli Değildir!).");

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  // Watchdog Zamanlayıcıyı her döngüde besle
  esp_task_wdt_reset(); 

  // Flash'a yazma işlemi için pin kontrolü
  static unsigned long buttonPressStartTime = 0; 
  if (digitalRead(SAVE_SETTINGS_PIN) == LOW) { 
    if (buttonPressStartTime == 0) { 
      buttonPressStartTime = millis();
      Serial.println("Ayarları kaydetmek için butonu basılı tutun...");
    } else if (millis() - buttonPressStartTime >= SAVE_DELAY_MS) {
      saveMqttCredentials();
      buttonPressStartTime = 0; 
      delay(500); 
    }
  } else { 
    buttonPressStartTime = 0; 
  }

  if (!client.connected()) {
    reconnect();
  }
  client.loop(); 

  // --- TCRT5000 (Hareket Algılama) ---
  int trctState = digitalRead(trctPin);
  bool currentLightState = (trctState == LOW);

  if (currentLightState != lightOn) {
    lightOn = currentLightState;
    digitalWrite(externalLedPin, lightOn ? HIGH : LOW); 
    
    String status = lightOn ? "on" : "off";
    client.publish(LIGHT_STATUS_TOPIC, status.c_str());
    Serial.print("Light Status Published: ");
    Serial.println(status);
  }

  // --- KY-027 (Deprem Uyarısı) ---
  int vibrationState = digitalRead(vibrationPin);
  
  if (vibrationState == LOW) { 
    if (millis() - lastVibrationTime > 50) { 
      vibrationCount++;
      lastVibrationTime = millis();
    }
  }

  if (millis() - lastVibrationTime > VIBRATION_WINDOW && vibrationCount > 0) {
      vibrationCount = 0;
  }

  if (vibrationCount >= VIBRATION_THRESHOLD && !earthquakeAlert) {
    earthquakeAlert = true;
    client.publish(EARTHQUAKE_ALERT_TOPIC, "ALERT");
    Serial.println("!!! DEPREM UYARISI YAYINLANDI !!!");
  } else if (vibrationCount < VIBRATION_THRESHOLD && earthquakeAlert) { 
    earthquakeAlert = false;
    client.publish(EARTHQUAKE_ALERT_TOPIC, "CLEAR");
    Serial.println("Deprem Uyarısı Temizlendi.");
  }

  delay(50); 
}