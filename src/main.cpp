#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ESP32Ping.h>
#include <time.h>
#include <sys/time.h>

// --- WiFi Settings ---
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// --- Firebase Settings ---
#define API_KEY "AIzaSyDUTppfzrKaS_nHiOG4Nb37aVkK1sCsYsQ"
#define DATABASE_URL "home-4a6c1-default-rtdb.firebaseio.com"

// --- Pins Definition ---
#define DHTPIN 15
#define DHTTYPE DHT22
#define MQ_PIN 35
#define LDR_PIN 34
#define LED_PIN 2
#define SERVO_PIN 18

// --- Objects ---
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo myServo;

FirebaseData fbdo;
FirebaseData stream;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
const long interval = 5000;

float temp = 0.0;
float hum = 0.0;
int mq_value = 0;
int ldr_value = 0;
unsigned long current_t1 = 0;
unsigned long current_t2 = 0;
bool pending_ack = false;
long long ack_cmd_id = 0;

struct Stat {
  double sum = 0;
  double min_val = 999999;
  double max_val = 0;
  int count = 0;

  void add(double val) {
    sum += val;
    if (val < min_val) min_val = val;
    if (val > max_val) max_val = val;
    count++;
  }
  double avg() { return count == 0 ? 0 : sum / count; }
};

Stat stat_t1, stat_t2, stat_t3, stat_tmon;
Stat stat_t4;

void connectWiFi();
void initFirebase();
void readSensors();
void displayData();
void testNetworkPing();
void streamCallback(StreamData data);
void streamTimeoutCallback(bool timeout);

void setup()
{
  Serial.begin(115200);

  pinMode(MQ_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  dht.begin();
  myServo.attach(SERVO_PIN);
  myServo.write(0);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println("Starting System...");
  display.display();

  connectWiFi();
  initFirebase();
}

void loop()
{
  if (Firebase.ready())
  {
    if (pending_ack)
    {
      pending_ack = false;
      FirebaseJson ackJson;
      ackJson.set("cmd_id", (double)ack_cmd_id);
      Firebase.updateNode(fbdo, "control/ack", ackJson);
    }

    if (millis() - sendDataPrevMillis > interval || sendDataPrevMillis == 0)
    {
      sendDataPrevMillis = millis();
      readSensors();
    displayData();

    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long esp_time =
        (long long)tv.tv_sec * 1000LL +
        tv.tv_usec / 1000;

    // Batch data into a single JSON object to optimize network latency
    FirebaseJson json;
    json.set("temperature", temp);
    json.set("humidity", hum);
    json.set("mq", mq_value);
    json.set("ldr", ldr_value);
    json.set("esp_time", esp_time);
    json.set("t1", current_t1 / 1000.0);
    json.set("t2", current_t2);
    json.set("server_time/.sv", "timestamp");
    
    // Sử dụng hàm Async (Bất đồng bộ) để không phải chờ đợi máy chủ phản hồi
    unsigned long upload_start = millis();

    bool ok = Firebase.updateNode(
        fbdo,
        "sensors",
        json
    );

    unsigned long upload_end = millis();

    if (ok)
    {
        current_t2 = upload_end - upload_start;
        Firebase.setInt(fbdo, "sensors/t2", current_t2);
    }
    else
    {
        Serial.printf(
            "[Firebase Error] %s\n",
            fbdo.errorReason().c_str()
        );
    }

    Serial.println();
    Serial.println("--------------------------------------------------");
    Serial.printf("[Firebase] Sent: Temp=%.1fC, Hum=%.1f%%, MQ=%d, LDR=%d\r\n", temp, hum, mq_value, ldr_value);
    testNetworkPing();
    }
  }
}

void connectWiFi()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nConnected to WiFi!");

  // Bắt buộc phải đồng bộ giờ Internet (NTP) để tính toán độ trễ t2, t5 chính xác
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  int retry = 0;
  // Chờ cho đến khi lấy được mốc thời gian thực tế (> năm 2023)
  while (now < 1700000000 && retry < 40)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    retry++;
  }
  Serial.println("");
}

void initFirebase()
{
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.signer.test_mode = true;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (!Firebase.beginStream(stream, "/control"))
  {
    Serial.printf("Stream begin error, %s\r\n", stream.errorReason().c_str());
  }
  Firebase.setStreamCallback(stream, streamCallback, streamTimeoutCallback);
}

void readSensors()
{
  unsigned long t1_start = micros();

  float new_temp = dht.readTemperature();
  float new_hum = dht.readHumidity();

  if (!isnan(new_temp) && !isnan(new_hum))
  {
    temp = new_temp;
    hum = new_hum;
  }

  mq_value = analogRead(MQ_PIN);
  ldr_value = analogRead(LDR_PIN);

  unsigned long t1 = micros() - t1_start;
  current_t1 = t1;
}

void displayData()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("--- Sensors ---");
  display.printf("Temp: %.1f C\n", temp);
  display.printf("Hum : %.1f %%\n", hum);
  display.printf("MQ  : %d\n", mq_value);
  display.printf("LDR : %d\n", ldr_value);

  if (WiFi.status() == WL_CONNECTED)
  {
    display.setCursor(0, 50);
    display.println("WiFi & FB: OK");
  }
  display.display();
}

void streamCallback(StreamData data)
{
  if (data.dataType() == "json")
  {
    FirebaseJson *json = data.jsonObjectPtr();
    FirebaseJsonData jsonData;

    if (data.dataPath() == "/led" || data.dataPath() == "/motor")
    {
      long long cmd_id = 0;
      json->get(jsonData, "cmd_id");
      if (jsonData.success) cmd_id = jsonData.doubleValue;

      json->get(jsonData, "value");
      if (jsonData.success)
      {
        if (data.dataPath() == "/led") {
          digitalWrite(LED_PIN, jsonData.boolValue ? HIGH : LOW);
        } else {
          int angle = jsonData.intValue;
          if (angle >= 0 && angle <= 180) {
            myServo.write(angle);
          }
        }

        ack_cmd_id = cmd_id;
        pending_ack = true;
      }
    }
    else if (data.dataPath() == "/latency_report")
    {
      double r_t4 = 0;
      
      json->get(jsonData, "t4");
      if (jsonData.success) {
        r_t4 = jsonData.doubleValue;
      } else {
        // Fallback do trình duyệt web của người dùng có thể bị cache file app.js cũ
        json->get(jsonData, "total");
        if (jsonData.success) r_t4 = jsonData.doubleValue;
      }
      
      stat_t4.add(r_t4);

      Serial.println();
      Serial.println("======= CONTROL LATENCY ======");
      Serial.printf("t4 Dashboard->Actuator : %.0f ms  (AVG: %.0f ms)\r\n", r_t4, stat_t4.avg());
      Serial.printf("TOTAL CONTROL          : %.0f ms  (AVG: %.0f ms)\r\n", r_t4, stat_t4.avg());
      Serial.printf("[Samples: %d]\r\n", stat_t4.count);
      Serial.println("==============================");
    }
    else if (data.dataPath() == "/monitoring_report")
    {
      double r_t1 = 0, r_t2 = 0, r_t3 = 0, r_total = 0;
      json->get(jsonData, "t1");
      if (jsonData.success) r_t1 = jsonData.doubleValue;
      json->get(jsonData, "t2");
      if (jsonData.success) r_t2 = jsonData.doubleValue;
      json->get(jsonData, "t3");
      if (jsonData.success) r_t3 = jsonData.doubleValue;
      json->get(jsonData, "total");
      if (jsonData.success) r_total = jsonData.doubleValue;

      stat_t1.add(r_t1);
      stat_t2.add(r_t2);
      stat_t3.add(r_t3);
      stat_tmon.add(r_total);

      Serial.println();
      Serial.println("===== MONITORING LATENCY =====");
      Serial.printf("t1 Sensor->ESP32       : %.3f ms  (AVG: %.3f ms)\r\n", r_t1, stat_t1.avg());
      Serial.printf("t2 ESP32->Firebase     : %.0f ms  (AVG: %.0f ms)\r\n", r_t2, stat_t2.avg());
      Serial.printf("t3 Firebase->Dashboard : %.0f ms  (AVG: %.0f ms)\r\n", r_t3, stat_t3.avg());
      Serial.printf("TOTAL MONITORING       : %.0f ms  (AVG: %.0f ms)\r\n", r_total, stat_tmon.avg());
      Serial.printf("[Samples: %d]\r\n", stat_tmon.count);
      Serial.println("==============================");
    }
  }
}

void streamTimeoutCallback(bool timeout)
{
  if (timeout)
  {
    Serial.println("Stream timeout, resuming...");
  }
}

void testNetworkPing()
{
  int num_pings = 5; // Reduced to 5 for faster checking
  int lost_pings = 0;
  float total_latency = 0;

  for (int i = 0; i < num_pings; i++)
  {
    if (Ping.ping("8.8.8.8", 1))
    {
      total_latency += Ping.averageTime();
    }
    else
    {
      lost_pings++;
    }
    delay(50);
  }

  float packet_loss = ((float)lost_pings / num_pings) * 100.0;
  float avg_latency = (num_pings > lost_pings) ? (total_latency / (num_pings - lost_pings)) : 0;

  Serial.printf("[Network]  Ping: %.1f ms | Loss: %.0f%%\r\n", avg_latency, packet_loss);
  Serial.println("--------------------------------------------------");
}
