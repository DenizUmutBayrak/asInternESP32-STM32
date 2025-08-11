#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// ESP32 Sistem Yeniden Başlatma Kütüphanesi
#include <esp_system.h>

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

// Kapasitif Buton Pini (GPIO 4 = T0)
const int CAPACITIVE_PIN = 4; 

// KAPASİTİF DOKUNMA EŞİĞİ: Bu değeri kendi testlerinize göre ayarlayın!
// Elinizi yaklaştırdığınızda 29500-30000 civarı görüyorsanız,
// 30500 gibi bir değer uygun olabilir.
const int CAPACITIVE_TOUCH_THRESHOLD = 30500; 

// BUTON SÜRELERİ
const unsigned long FLASH_SAVE_SHORT_PRESS_MAX_MS = 500; // Flash'a kaydetmek için kısa dokunuş (500ms'den az)
const unsigned long WATCHDOG_TRIGGER_HOLD_TIME_MS = 3000; // Watchdog'u tetiklemek için basılı tutma süresi (3 saniye)
const unsigned long WATCHDOG_TRIGGER_DELAY_MS = 12000; // Watchdog'u tetikleyecek kasıtlı gecikme süresi (12 saniye)

// Sensör Durumları ve Kontrol Değişkenleri
bool lightOn = false;
bool earthquakeAlert = false;
unsigned long lastVibrationTime = 0;
int vibrationCount = 0;
const int VIBRATION_THRESHOLD = 5;        
const unsigned long VIBRATION_WINDOW = 500; 

// Kapasitif Buton İçin Değişkenler
static unsigned long capacitiveButtonPressStartTime = 0;
static bool capacitiveButtonPressed = false; 
static bool actionTriggeredDuringPress = false; // Basılı tutma sırasında eylemin tetiklenip tetiklenmediğini kontrol eder

WiFiClientSecure espClient; 
PubSubClient client(espClient);

// Watchdog Zamanlayıcı Tanımlaması
const int WATCHDOG_TIMEOUT_SEC = 10; // Watchdog zaman aşımı süresi (saniye)

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

  // Watchdog Timer başlatma
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WATCHDOG_TIMEOUT_SEC * 1000, 
    .idle_core_mask = (1 << 0), 
    .trigger_panic = false 
  };
  esp_task_wdt_init(&wdt_config); 
  esp_task_wdt_add(NULL); 
  Serial.print("Watchdog Timer Started with "); Serial.print(WATCHDOG_TIMEOUT_SEC); Serial.println(" seconds timeout.");

  // Flash Bellek Preferences'ı başlatma
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

  // --- Kapasitif Buton Kontrolü (GPIO 4) ---
  int touchValue = touchRead(CAPACITIVE_PIN); 

  // Debug için dokunmatik değeri görmek istersen aşağıdaki satırları aktif edebilirsin
   Serial.print("Dokunmatik Deger (GPIO4): "); 
   Serial.println(touchValue); 

  if (touchValue > CAPACITIVE_TOUCH_THRESHOLD) { // Butona dokunuluyor (veya el yaklaştırıldı)
    if (!capacitiveButtonPressed) { // Butona ilk basış başlangıcı
      capacitiveButtonPressStartTime = millis();
      actionTriggeredDuringPress = false; // Yeni bir basışta eylem tetiklenmedi olarak işaretle
      Serial.println("Kapasitif alana dokunuldu/yaklaşıldı.");
    }
    capacitiveButtonPressed = true;

    // Uzun basış (Watchdog Tetikleme) kontrolü - basılı tutarken tetiklenir
    if (!actionTriggeredDuringPress && millis() - capacitiveButtonPressStartTime >= WATCHDOG_TRIGGER_HOLD_TIME_MS) {
      Serial.println("!!! Uzun basma/yaklasim algilandi. Watchdog'u kasitli olarak tetikleyen gecikme baslatiliyor...");
      Serial.println("!!! Bu surede Watchdog beslenmeyecek ve sistem yeniden baslayacak. !!!");
      actionTriggeredDuringPress = true; // Sadece bir kez tetiklenmesini sağla
      delay(WATCHDOG_TRIGGER_DELAY_MS); // Kasıtlı olarak uzun bir gecikme
      // Bu gecikme sırasında loop() içindeki esp_task_wdt_reset() çağrılamaz.
    }
  } else { // Buton bırakıldı (veya el uzaklaştırıldı)
    if (capacitiveButtonPressed) { // Buton az önce bırakıldıysa
      unsigned long pressDuration = millis() - capacitiveButtonPressStartTime;

      // Kısa basış (Flash Kaydetme) kontrolü - bırakıldığında tetiklenir
      if (!actionTriggeredDuringPress && pressDuration < FLASH_SAVE_SHORT_PRESS_MAX_MS) {
        saveMqttCredentials();
        Serial.println("Flash'a kaydetme islemi tamamlandi (kisa dokunus ile).");
      }
      // Eğer actionTriggeredDuringPress true ise (yani uzun basma Watchdog'u tetiklediyse)
      // veya süre kısa dokunuş eşiğini aştıysa, hiçbir şey yapma (uzun basış veya orta basış)
    }
    capacitiveButtonPressed = false;
    actionTriggeredDuringPress = false; // Bir sonraki basış için sıfırla
  }

  // MQTT bağlantısını kontrol et ve döngüyü çalıştır
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); 

  // --- TCRT5000 (Hareket Algılama) ---
  int trctState = digitalRead(trctPin);
  bool currentLightState = (trctState == LOW); // LOW olduğunda ışık var

  if (currentLightState != lightOn) {
    lightOn = currentLightState;
    digitalWrite(externalLedPin, lightOn ? HIGH : LOW); 
    
    String status = lightOn ? "on" : "off";
    client.publish(LIGHT_STATUS_TOPIC, status.c_str());
    Serial.print("Light Status Published: ");
    Serial.println(status);
  }

  // --- KY-027 (Deprem Uyarısı) ---
  int vibrationState = digitalRead(vibrationPin); // Titreşim sensöründen oku
  
  if (vibrationState == LOW) { // Titreşim algılandığında (LOW tetikleme)
    if (millis() - lastVibrationTime > 50) { // Gürültüyü engellemek için küçük bir gecikme
      vibrationCount++;
      lastVibrationTime = millis();
    }
  }

  // Titreşim sayacını sıfırlamak için zaman penceresi
  if (millis() - lastVibrationTime > VIBRATION_WINDOW && vibrationCount > 0) {
      vibrationCount = 0;
  }

  // Belirli sayıda titreşim algılandığında deprem uyarısı yayınla
  if (vibrationCount >= VIBRATION_THRESHOLD && !earthquakeAlert) {
    earthquakeAlert = true;
    client.publish(EARTHQUAKE_ALERT_TOPIC, "ALERT");
    Serial.println("!!! DEPREM UYARISI YAYINLANDI !!!");
  } else if (vibrationCount < VIBRATION_THRESHOLD && earthquakeAlert) { // Titreşim yoksa uyarıyı temizle
    earthquakeAlert = false;
    client.publish(EARTHQUAKE_ALERT_TOPIC, "CLEAR"); 
    Serial.println("Deprem Uyarısı Temizlendi.");
  }

  delay(50); // Loop döngüsü için küçük bir gecikme
}