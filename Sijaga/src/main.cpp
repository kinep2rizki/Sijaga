#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <WiFiManager.h>

// Pin definitions
const int lock = 21;
const int buzzer = 32;
const int LED_R = 12;
const int LED_G = 14;
//const int button = 33;
const int pinGetar = 15;
const int pinBuzz = 32;

#define RELAY_PIN 21  
#define TRIG_PIN 26
#define ECHO_PIN 25
#define RST_PIN 4
#define SS_PIN 5

// Deklarasi variabel sensor getar
unsigned long pulseDuration = 0;  // Durasi pulsa yang diterima dari sensor
int buzzerLevel = 0;

//Define database & endpoint
String API_URL = ""; //link dari api (url)
String PostUID = ""; //Endpoint Post UID
String PostLog = ""; //Endpoint post LogStatus
String endpointStatusBarang = ""; //endpoint availability status
String UsageHistory = ""; //endpoint usage history
const int httpsPort = 443;

//status solenoid
String solenoidStatus = "KUNCI";

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
void setupWiFi();
void ukurjarak();
void SensorGetar();
void ReadRFID();
void StatusBarang(String status);
void sendUidToDatabase(String uid);
void ControlSolenoid(String uid);
bool checkAuthorization(String uid);
void logSolenoidStatus(String uid, String time, String status);
void historypemakaian(String cardId, String status, String solenoidStatus);
String getFormattedTime();
void IRAM_ATTR handleGetar();

void setup() {
    // Initialize serial communication
    Serial.begin(115200);

    // Initialize pins
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // Pastikan solenoid terkunci saat startup

    pinMode(lock, OUTPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(buzzer, OUTPUT);
    // pinMode(button, INPUT_PULLUP);
    pinMode(pinGetar, INPUT);
    pinMode(pinBuzz, OUTPUT);
    digitalWrite(lock, HIGH);
    digitalWrite(LED_R, LOW);
    digitalWrite(LED_G, HIGH);

    // Debugging status relay
    Serial.println("System initialized. Solenoid is locked.");

    // Connect to Wi-Fi
    setupWiFi();
    // WiFiClientSecure client;
    // client.setInsecure();  // Menonaktifkan verifikasi sertifikat SSL


    // Initialize SPI and RFID
    SPI.begin();
    mfrc522.PCD_Init();

    // Adjust antenna gain
    mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_avg);

    if (!mfrc522.PCD_PerformSelfTest()) {
        Serial.println("RFID self-test failed.");
    } else {
        Serial.println("RFID initialized successfully.");
    }

    // Configure time
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    // Interrupt setup
    attachInterrupt(digitalPinToInterrupt(pinGetar), handleGetar, RISING);
}

void loop() {
    Serial.println("Relay State: " + String(digitalRead(RELAY_PIN)));
    ReadRFID();
    ukurjarak();
    SensorGetar();
    
    // Reset pembaca RFID agar siap membaca kartu berikutnya
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    //RefreshSistem();

    delay(500);
}

void setupWiFi() {
    WiFiManager wifiManager;
    // Set Debug Output
    wifiManager.setDebugOutput(true);
    // Debugging: Indicate WiFiManager start
    Serial.println("Starting WiFiManager...");

    // WiFiManager AutoConnect with fallback portal
    if (!wifiManager.autoConnect("Sijaga", "sijaga123")) {
        Serial.println("Failed to connect, restarting...");
        delay(3000);
        ESP.restart();
    }

    // Debugging: Indicate successful connection
    Serial.println("Wi-Fi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

void ukurjarak() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH);
    int distance_cm = (duration /2 ) / 29.1;

    Serial.print("Distance: ");
    Serial.print(distance_cm);
    Serial.println(" cm");

    if (distance_cm < 55) {
        digitalWrite(LED_R, HIGH);
        digitalWrite(LED_G, LOW);
        status = "ADA BARANG";
    } else {
        digitalWrite(LED_R, LOW);
        digitalWrite(LED_G, HIGH);
        status = "TIDAK ADA BARANG";
    }

    Serial.println(status);
    Serial.println(" ");
    delay(1000);
}

void SensorGetar() {
    pulseDuration = pulseIn(pinGetar, HIGH);  // Mengukur durasi pulsa HIGH

    // Menentukan tingkat keparahan getaran berdasarkan durasi pulsa
    // Fuzzy membership function untuk keanggotaan rendah, sedang, dan tinggi

    // Keanggotaan untuk "Rendah" (0 - 500 mikrodetik)
    float membershipRendah = constrain(map(pulseDuration,0,900,1,0),0,1);  // Jika durasi pulsa <= 500us, tingkat rendah

    // Keanggotaan untuk "Sedang" (500 - 1000 mikrodetik)
    float membershipSedang = constrain(map(pulseDuration,900, 1250, 0, 1), 0, 1);  // Jika durasi pulsa antara 500 - 1000us

    // Keanggotaan untuk "Tinggi" (1000 - 3000 mikrodetik)
    float membershipTinggi = constrain(map(pulseDuration, 1250, 3000, 0, 1), 0, 1);  // Jika durasi pulsa >= 1000us, tingkat tinggi

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

        unsigned long startTime = millis(); // Catat waktu awal
        while (millis() - startTime < 3000) { // Loop selama 3 detik
            digitalWrite(pinBuzz, HIGH); // Nyalakan buzzer
            delay(100);                  // Durasi suara aktif
            digitalWrite(pinBuzz, LOW);  // Matikan buzzer
            delay(100);                  // Durasi jeda sebelum aktif lagi
        }
    } else {
        digitalWrite(pinBuzz, LOW);       // Matikan buzzer jika tidak ada getaran tinggi
    }
}

void ReadRFID() {
    uid = ""; // Clear previous UID

    // Check if a card is present
    if (mfrc522.PICC_IsNewCardPresent()) {
        Serial.println("Fungsi ini terpanggil");
        if (mfrc522.PICC_ReadCardSerial()) {
            // Read the UID of the RFID card
            byte cardUID[4];
            for (byte i = 0; i < 4; i++) {
                cardUID[i] = mfrc522.uid.uidByte[i];
            }

            Serial.print("Card UID: ");
            for (byte i = 0; i < 4; i++) {
                Serial.print(cardUID[i], HEX);
                Serial.print(" ");
                uid += String(cardUID[i] < 0x10 ? "0" : "");
                uid += String(cardUID[i], HEX);
            }
            Serial.println();
            uid.toUpperCase();
            Serial.println("RFID Detected UID: " + uid);

            // Ensure UID is not empty
            if (uid.length() == 0) {
                Serial.println("UID is empty, cannot proceed.");
                return;
            }

            // Check authorization
            bool isAuthorized = checkAuthorization(uid);

            if (isAuthorized) {
                Serial.println("UID Match: Authorized!");

                // Control solenoid (change lock status)
                ControlSolenoid(uid);

                // Send UID to database
                sendUidToDatabase(uid);

                // Send item status
                StatusBarang(status);

                // Determine availability status
                //String availStatus = (status == "ADA BARANG") ? "Available" : "Not Available";
                historypemakaian(uid, status, solenoidStatus); // Send history after successful tap
            } else {
                Serial.println("UID Not Found: Access Denied.");

                // Activate buzzer if access is denied
                digitalWrite(buzzer, HIGH);
                delay(1000);
                digitalWrite(buzzer, LOW);

                // Log to database
                sendUidToDatabase(uid);
            }
        }
    }            
    // Reset communication with the RFID card
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
}

void ControlSolenoid(String uid) {
    if (checkAuthorization(uid)) { // Cek apakah UID memiliki izin
        Serial.println("UID Authorized: " + uid);

        // Ubah status solenoid berdasarkan tap
        if (isFirstTap) {
            Serial.println("Unlocking solenoid...");
            digitalWrite(RELAY_PIN, LOW); // Relay aktif (solenoid buka)
            tap = "ACTIVE";
        } else {
            Serial.println("Locking solenoid...");
            digitalWrite(RELAY_PIN, HIGH); // Relay nonaktif (solenoid kunci)
            tap = "INACTIVE";
        }

        // Perbarui status solenoid global
        solenoidStatus = tap;

        // Debugging
        Serial.println("Solenoid Status: " + solenoidStatus);

        delay(3000); // Tambahkan delay untuk memastikan stabilitas relay

        // Perbarui log status solenoid
        String currentTime = getFormattedTime();
        logSolenoidStatus(uid, currentTime, tap);

        // Periksa status barang menggunakan ultrasonik
        ukurjarak();

        // Ubah status tap
        isFirstTap = !isFirstTap;
    } else {
        Serial.println("Access Denied: Unauthorized UID");
        digitalWrite(buzzer, HIGH); // Buzzer menyala untuk akses ditolak
        delay(1000);
        digitalWrite(buzzer, LOW);

        // Reset pembaca RFID agar siap membaca kartu berikutnya
        // mfrc522.PICC_HaltA();
        // mfrc522.PCD_StopCrypto1();
    }

    return;
}


bool checkAuthorization(String uid) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected, cannot check authorization.");
        return false;
    }

    HTTPClient http;
    String query = API_URL + "/history/users?uid=eq." + uid + "&select=*";
    http.begin(query);

    // Debugging log sebelum GET request
    //Serial.println("GET Request URL: " + query);

    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
        String payload = http.getString();
        // Serial.println("GET Response:");
        // Serial.println(payload);

        // Validasi jika payload berisi UID yang sesuai
        if (payload.indexOf(uid) > -1) {  // Cari UID dalam respons
            Serial.println("UID Found: Authorized.");
            http.end();
            return true;
        } else {
            Serial.println("UID Not Authorized.");
        }
    } else {
        Serial.print("Error on GET request. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }

    http.end();
    return false;  // Default: Unauthorized jika tidak ada UID cocok
}

void logSolenoidStatus(String uid, String time, String status) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected, cannot log status.");
        return;
    }

    HTTPClient http;
    String logEndpoint = API_URL + PostLog; // Endpoint untuk log status
    http.begin(logEndpoint); // Tidak menggunakan API key
    http.addHeader("Content-Type", "application/json");

    // JSON payload
    String payload = "{";
    payload += "\"uid\":\"" + uid + "\",";
    payload += "\"time\":\"" + time + "\",";
    payload += "\"status\":\"" + status + "\"";
    payload += "}";

    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("POST Response:");
        Serial.println(response);
    } else {
        Serial.print("Error on POST request. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }

    http.end();
}

void sendUidToDatabase(String uid) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected, cannot send UID to database.");
        return;
    }

    if (uid.length() == 0) {
        Serial.println("UID is empty, cannot send to database.");
        return;
    }

    HTTPClient http;
    String endpoint = "https://sijaga-railway-production.up.railway.app/card-id/create"; // Endpoint dari gambar
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json");

    // JSON payload
    String payload = "{\"cardId\":\"" + uid + "\"}";
    Serial.println("Payload: " + payload); // Debugging

    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("POST Response:");
        Serial.println(response);
    } else {
        Serial.print("Error on sending POST: ");
        Serial.println(httpResponseCode);
    }
    http.end();
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

void StatusBarang(String status) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected, cannot send status to database.");
        return;
    }

    HTTPClient http;
    String endpoint = API_URL + endpointStatusBarang; // Endpoint untuk POST data status barang
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json"); // Header untuk JSON payload

    // JSON payload
    String payload = "{";
    payload += "\"status\":\"" + status + "\"";
    payload += "}";

    // Debugging URL dan Payload
    Serial.println("Endpoint: " + endpoint);
    Serial.println("Payload: " + payload);

    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("POST Response:");
        Serial.println(response);
    } else {
        Serial.print("Error on POST request. HTTP Response code: ");
        Serial.println(httpResponseCode);
    }

    http.end();
}

void historypemakaian(String cardId, String status, String solenoidStatus) {
    // Membentuk payload JSON
    String payload = "{";
    payload += "\"card_id\":\"" + cardId + "\","; // ID kartu RFID
    payload += "\"status\":\"" + solenoidStatus + "\",";  // Status penggunaan
    payload += "\"availStatus\":\"" + status + "\""; // Status solenoid
    payload += "}";

    // Menampilkan payload ke Serial Monitor (untuk debugging)
    Serial.println("Payload to send:");
    Serial.println(payload);

    // Membuat koneksi HTTP dan mengirimkan payload ke server
    if (WiFi.status() == WL_CONNECTED) { // Periksa koneksi Wi-Fi
        HTTPClient http;

        // URL tujuan (ganti dengan URL endpoint Anda)
        String url = API_URL + UsageHistory; // Update with your actual endpoint
        http.begin(url); // Inisialisasi HTTPClient dengan URL
        http.addHeader("Content-Type", "application/json"); // Tambahkan header untuk JSON

        // Kirim payload menggunakan metode POST
        int httpResponseCode = http.POST(payload);

        // Menampilkan respons dari server
        if (httpResponseCode > 0) {
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            String response = http.getString();
            Serial.println("Server response:");
            Serial.println(response);
        } else {
            Serial.print("Error on sending POST: ");
            Serial.println(httpResponseCode);
        }

        http.end(); // Mengakhiri koneksi HTTP
    } else {
        Serial.println("WiFi Disconnected. Cannot send data.");
    }
}

void IRAM_ATTR handleGetar() {
    getaranTerdeteksi = true; // Set flag saat ada getaran
}