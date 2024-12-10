#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

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

//Define database
String API_URL = ""; //api url
String API_KEY = ""; //apikey
String TableName = ""; //table name
const int httpsPort = 443;

// Wi-Fi configuration
#define WIFI_SSID "CPSRG"
#define WIFI_PASSWORD "CPSJAYA123"

// RFID setup
MFRC522 mfrc522(SS_PIN, RST_PIN);
String uidString = "";
String status = "";
bool isFirstTap = true;
bool refresh = false;
String tap = "KUNCI";

WiFiClientSecure client;

// Function declarations
void connectWiFi();
void ukurjarak();
void SensorGetar();
void ReadRFID();
void RefreshSistem();

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

  // Initialize SPI and RFID
  SPI.begin();
  mfrc522.PCD_Init();

  // Connect to Wi-Fi
  connectWiFi();

  //melakukan config waktu
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
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
}

void SensorGetar() {
  long NilaiGetar = pulseIn(pinGetar, HIGH);
  Serial.print("Nilai Getaran: ");
  Serial.println(NilaiGetar);

  if (NilaiGetar == 0) {
    Serial.println("Getaran Tidak terdeteksi");
    digitalWrite(pinBuzz, LOW);
  } else if (NilaiGetar > 5000) {
    Serial.println("Alat Bergetar");
    digitalWrite(pinBuzz, HIGH);
  }
}

void ReadRFID(String &uidString) {
  uidString = "";  // Clear previous UID string
  
  // Check for new card
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;  // No card detected, exit the function
  }

  // Read UID of card
  Serial.print("UID: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uidString += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    uidString += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidString.toUpperCase();
  
  Serial.println(uidString);  // Print the UID
}

// Function to control the solenoid (lock)
void ControlSolenoid(String uidString) {
  bool authorized = (uidString == "551E9552" || uidString == "1637C942" || uidString == "8518D952");
  
  if (authorized) {
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
