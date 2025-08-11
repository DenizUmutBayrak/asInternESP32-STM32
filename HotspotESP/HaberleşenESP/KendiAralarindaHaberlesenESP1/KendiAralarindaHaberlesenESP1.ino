#include <WiFi.h>
#include <HTTPClient.h>      // ESP-2'ye HTTP ile veri göndermek için
#include <DHT.h>             // DHT11 sensörü için
#include <driver/rtc_io.h>   // Derin uyku için GPIO ayarları
#include <esp_sleep.h>       // esp_sleep_enable_timer_wakeup için gereklidir

// --- Wi-Fi Bilgileri (ESP-2'nin Access Point bilgileri) ---
// Bu bilgiler ESP-2 kodundaki ap_ssid ve ap_password ile BİREBİR AYNI OLMALIDIR!
const char* ssid = "MyESP32Server";       // ESP-2'nin Wi-Fi adı
const char* password = "mypassword";      // ESP-2'nin Wi-Fi şifresi (en az 8 karakter)
// BURADA DİKKAT: serverIP ve serverPort GLOBAL KAPSAMDA OLMALI!
const char* serverIP = "192.168.4.1";     // ESP-2'nin SoftAP IP adresi (GENELLİKLE BU OLUR, ESP-2 Seri Monitörden kontrol edin)
const int serverPort = 80;                // ESP-2'nin dinlediği port (HTTP için standart)

// --- DHT11 Sensör Ayarları ---
#define DHTPIN 15         // DHT11 Data pini bağlı olduğu GPIO
#define DHTTYPE DHT11     // DHT11 sensör tipi
DHT dht(DHTPIN, DHTTYPE);

// --- LED Ayarları ---
#define LED_PIN 16        // Veri okunduğunda yanacak LED'in bağlı olduğu GPIO (GPIO 16 kullanıldı)

// --- Derin Uyku Ayarları ---
#define uS_TO_S_FACTOR 1000000  // Mikrosaniyeyi saniyeye çevirme faktörü
#define TIME_TO_SLEEP 5       // ESP'nin uyuyacağı süre (saniye)

RTC_DATA_ATTR int bootCount = 0; // Uyku modundan kaç kez uyandığımızı saymak için

// --- Fonksiyon Tanımlamaları ---
void sendDataToESP2(float humidity, float temperature);
void enterDeepSleep();

void setup() {
  Serial.begin(115200);
  Serial.println();

  // Uyku modundan uyanma sebebini kontrol et (debug için)
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  Serial.print("Uyanma Sebebi: ");
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Harici pin (EXT0)"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Harici pin (EXT1)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Zamanlayıcı"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Dokunmatik yüzey"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("ULP"); break;
    default: Serial.println("Güç sıfırlama veya diğer nedenler"); break;
  }

  ++bootCount;
  Serial.printf("Başlangıç sayısı: %d\n", bootCount);

  pinMode(LED_PIN, OUTPUT);     // LED pinini çıkış olarak ayarla
  digitalWrite(LED_PIN, LOW);   // Başlangıçta LED'i kapat

  dht.begin(); // DHT sensörünü başlat

  Serial.print("ESP-2'nin Wi-Fi ağına bağlanılıyor: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { // 10 saniye bekle (20 * 500ms)
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi Bağlandı!");
    Serial.print("Kendi IP adresi: ");
    Serial.println(WiFi.localIP());

    // DHT11'den veri oku
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
      Serial.println("DHT sensöründen veri okunamadı!");
    } else {
      Serial.print("Nem: ");
      Serial.print(h);
      Serial.print(" %\t");
      Serial.print("Sıcaklık: ");
      Serial.print(t);
      Serial.println(" *C");

      digitalWrite(LED_PIN, HIGH); // Veri okunduğunda LED'i yak
      delay(1000); // LED'in yanık kalma süresi
      digitalWrite(LED_PIN, LOW);  // LED'i kapat

      // Veriyi ESP-2'ye gönder
      sendDataToESP2(h, t);
    }
  } else {
    Serial.println("Wi-Fi bağlantısı başarısız oldu. ESP-2 AP'si bulunamadı veya şifre yanlış.");
  }

  // İşlem bitti, derin uykuya geç
  enterDeepSleep();
}

void loop() {
  // Bu döngüye erişilmeyecek çünkü setup'tan sonra doğrudan uykuya geçiyoruz
}

void sendDataToESP2(float humidity, float float_temperature) {
  HTTPClient http;
  // serverIP ve serverPort değişkenleri burada doğrudan kullanılabiliyor olmalı
  String serverPath = "http://" + String(serverIP) + ":" + String(serverPort) + "/data"; // serverIP burada kullanılabilir

  // JSON formatında veri oluştur
  String jsonPayload = "{\"humidity\":" + String(humidity) + ",\"temperature\":" + String(float_temperature) + "}";

  Serial.print("ESP-2'ye gönderiliyor: ");
  Serial.println(jsonPayload);

  http.begin(serverPath);
  http.addHeader("Content-Type", "application/json"); // JSON gönderdiğimizi belirt

  int httpResponseCode = http.POST(jsonPayload); // POST isteği gönder

  if (httpResponseCode > 0) {
    Serial.print("HTTP Yanıt Kodu: ");
    Serial.println(httpResponseCode);
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.print("HTTP İsteği Başarısız Oldu, Hata: ");
    Serial.println(httpResponseCode);
  }
  http.end(); // Bağlantıyı kapat
}

void enterDeepSleep() {
  Serial.println("Derin uykuya geçiliyor...");
  Serial.printf("Sonraki uyanma %d saniye sonra.\n", TIME_TO_SLEEP);

  // Zamanlayıcı ile uyanmayı etkinleştir
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  // Sadece RTC destekli ve kullanılmayan GPIO'ları deinit edin
  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    gpio_num_t gpio = (gpio_num_t)i;
    if (rtc_gpio_is_valid_gpio(gpio) && gpio != DHTPIN && gpio != LED_PIN) {
      rtc_gpio_deinit(gpio);
    }
  }

  esp_deep_sleep_start(); // Derin uykuya başla
}