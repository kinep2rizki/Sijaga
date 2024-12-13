#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// Pin definitions
const int lock = 21;
const int buzzer = 32;
const int LED_R = 12;
const int LED_G = 14;
const int button = 33;
const int pinGetar = 15;
const int pinBuzz = 32;

#define TRIG_PIN 18
#define ECHO_PIN 5
#define RST_PIN 4
#define SS_PIN 2

// Deklarasi variabel sensor getar
unsigned long pulseDuration = 0;  // Durasi pulsa yang diterima dari sensor
int buzzerLevel = 0;

//Define database
String API_URL = "https://umcccazrujiewjrlxjvv.supabase.co"; //link dari api (url)
String API_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVtY2NjYXpydWppZXdqcmx4anZ2Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3MzM0OTk2MDYsImV4cCI6MjA0OTA3NTYwNn0.Oau8UXNtyd6CKUKuXo08LgK8M4QxEiHVhJ14WfjXskc"; //apikey
String TableName = "card_id_dumps"; //table name
const int httpsPort = 443;

// Wi-Fi configuration
#define WIFI_SSID "CPSRG"
#define WIFI_PASSWORD "CPSJAYA123"

// RFID setup
MFRC522 mfrc522(SS_PIN, RST_PIN);
String uid = "";
String status = "";
bool isFirstTap = true;
bool refresh = false;
String tap = "KUNCI";

// Deklarasi Interrupt untuk sensor getar
volatile bool getaranTerdeteksi = false;

WiFiClientSecure client;

// Function declarations
void connectWiFi();
void ukurjarak();
void SensorGetar();
void ReadRFID();
void RefreshSistem();
void sendUidToDatabase(String uid);
void ControlSolenoid(String uid);
bool checkAuthorization(String uid);
void logSolenoidStatus(String uid, String time, String status);
String getFormattedTime();
void IRAM_ATTR handleGetar();

void setup() {
  // Initialize serial communication
  Serial.begin(115200);

  // Initialize pins
  pinMode(lock, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(button, INPUT_PULLUP);
  pinMode(pinGetar, INPUT);
  pinMode(pinBuzz, OUTPUT);
  digitalWrite(lock, HIGH);
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, HIGH);

  // Connect to Wi-Fi
  connectWiFi();

  // Initialize SPI and RFID
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("Scan Kartu Rfid anda : ");

  //melakukan config waktu
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

// Interrupt pada sinyal naik
  attachInterrupt(digitalPinToInterrupt(pinGetar), handleGetar, RISING); 
}

void loop() {
  ukurjarak();
  SensorGetar();
  ReadRFID();
  RefreshSistem();
}

// Function implementations
void connectWiFi() {
  Serial.print("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected to Wi-Fi. IP Address: ");
  Serial.println(WiFi.localIP());
}

void ukurjarak() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  int distance_cm = (duration / 2) / 29.1;

  Serial.print("Distance: ");
  Serial.print(distance_cm);
  Serial.println(" cm");

  if (distance_cm < 30) {
    digitalWrite(LED_R, HIGH);
    digitalWrite(LED_G, LOW);
    status = "ADA BARANG";
  } else {
    digitalWrite(LED_R, LOW);
    digitalWrite(LED_G, HIGH);
    status = "TIDAK ADA BARANG";
  }

  delay(1000);
}

void SensorGetar() {
  pulseDuration = pulseIn(pinGetar, HIGH);  // Mengukur durasi pulsa HIGH

  // Menentukan tingkat keparahan getaran berdasarkan durasi pulsa
  // Fuzzy membership function untuk keanggotaan rendah, sedang, dan tinggi

  // Keanggotaan untuk "Rendah" (0 - 500 mikrodetik)
  float membershipRendah = constrain(map(pulseDuration,0,500,1,0),0,1);  // Jika durasi pulsa <= 500us, tingkat rendah

  // Keanggotaan untuk "Sedang" (500 - 1000 mikrodetik)
  float membershipSedang = constrain(map(pulseDuration,900, 1000, 0, 1), 0, 1);  // Jika durasi pulsa antara 500 - 1000us

  // Keanggotaan untuk "Tinggi" (1000 - 3000 mikrodetik)
  float membershipTinggi = constrain(map(pulseDuration, 1000, 3000, 0, 1), 0, 1);  // Jika durasi pulsa >= 1000us, tingkat tinggi

  // Debugging untuk melihat hasil keanggotaan
  Serial.print("Pulse Duration: ");
  Serial.print(pulseDuration);
  Serial.print(" | Rendah: ");
  Serial.print(membershipRendah);
  Serial.print(" | Sedang: ");
  Serial.print(membershipSedang);
  Serial.print(" | Tinggi: ");
  Serial.println(membershipTinggi);

  // Logika untuk mengendalikan buzzer berdasarkan membership fuzzy
  if (membershipTinggi > 0.5) {
    buzzerLevel = 1;  // Jika keanggotaan tinggi > 0.5, nyalakan buzzer
  } else if (membershipSedang > 0.5) {
    buzzerLevel = 1;  // Jika keanggotaan sedang > 0.5, nyalakan buzzer
  } else {
    buzzerLevel = 0;  // Jika keanggotaan rendah, matikan buzzer
  }

  // Mengontrol buzzer berdasarkan level
  if (buzzerLevel == 1) {
    Serial.println("Alat Bergetar (Buzzer Aktif)");
    digitalWrite(pinBuzz, HIGH);  // Aktifkan buzzer
    delay(500);                   // Durasi bunyi buzzer
    digitalWrite(pinBuzz, LOW);   // Matikan buzzer
  } else {
    digitalWrite(pinBuzz, LOW);   // Matikan buzzer jika tidak ada getaran tinggi
  }

}

void ReadRFID() {
     uid = ""; // Clear previous UID string

    // Check for new card
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
        return; // No card detected, exit the function
    }

    // Read UID of card
    Serial.print("UID: ");
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        uid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        uid += String(mfrc522.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();
    Serial.println(uid); // Print the UID to serial monitor

    // Send UID to database
    sendUidToDatabase(uid);
}


// Function to control the solenoid (lock)
void ControlSolenoid(String uid) {
  if (checkAuthorization(uid)) {
    Serial.println("Authorized card detected");
    if (isFirstTap) {
      digitalWrite(lock, LOW);  // Unlock the solenoid
      digitalWrite(pinBuzz, LOW);
      tap = "BUKA";
      isFirstTap = false;
    } else {
      digitalWrite(lock, HIGH); // Lock the solenoid
      tap = "KUNCI";
      isFirstTap = true;
    }
  } else {
    Serial.println("Access denied");
    digitalWrite(buzzer, HIGH);
    delay(1000);
    digitalWrite(buzzer, LOW);
  }
}

bool checkAuthorization(String uid) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected, cannot check authorization.");
    return false;
  }

  HTTPClient https;
  String query = API_URL + "/v1/" + TableName + "?uid=eq." + uid;
  https.begin(query);
  https.addHeader("apikey", API_KEY);
  
  int httpResponseCode = https.GET();

  if (httpResponseCode > 0) {
    Serial.println("Received response from server");
    String payload = https.getString();
    https.end();

    if (payload.length() > 2) { // UID is authorized
      String currentTime = getFormattedTime();
      String solenoidStatus = isFirstTap ? "Terbuka" : "Terkunci";
      Serial.println("Authorization successful. Logging time and status to database.");
      
      // Log time and status to database
      logSolenoidStatus(uid, currentTime, solenoidStatus);

      return true;
    }
  } else {
    Serial.print("Error on HTTP request: ");
    Serial.println(httpResponseCode);
    https.end();
  }

  return false;
}

void logSolenoidStatus(String uid, String time, String status) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected, cannot log status.");
    return;
  }

  HTTPClient https;
  String logEndpoint = API_URL + "/v1/logTable"; // Replace "logTable" with your log table name
  https.begin(logEndpoint);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("apikey", API_KEY);

  // Create JSON payload
  String payload = "{";
  payload += "\"uid\":\"" + uid + "\",";
  payload += "\"time\":\"" + time + "\",";
  payload += "\"status\":\"" + status + "\"";
  payload += "}";

  int httpResponseCode = https.POST(payload);

  if (httpResponseCode > 0) {
    Serial.println("Log successfully saved to database.");
  } else {
    Serial.print("Failed to log status. HTTP Response code: ");
    Serial.println(httpResponseCode);
  }

  https.end();
}

void RefreshSistem() {
  if (digitalRead(button) == LOW) {
    refresh = true;
  } else if (refresh) {
    Serial.println("System refreshed");
    digitalWrite(lock, LOW);
    digitalWrite(pinBuzz, LOW);
    delay(1000);
    ESP.restart();
    refresh = false;
  }
}

void sendUidToDatabase(String uid) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected, cannot send UID to database.");
        return;
    }

    HTTPClient https;
    String endpoint = API_URL + "/v1/" + TableName;
    https.begin(endpoint);
    https.addHeader("Content-Type", "application/json");
    https.addHeader("apikey", API_KEY);

    // Create JSON payload
    String payload = "{";
    payload += "\"uid\":\"" + uid + "\"";
    payload += "}";

    int httpResponseCode = https.POST(payload);

    if (httpResponseCode > 0) {
        Serial.println("UID successfully sent to database.");
    } else {
        Serial.print("Failed to send UID. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }

    https.end();
}

String getFormattedDate() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
  return String(dateStr);
}

String getFormattedTime() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(timeStr);
}

void IRAM_ATTR handleGetar() {
    getaranTerdeteksi = true; // Set flag saat ada getaran
}