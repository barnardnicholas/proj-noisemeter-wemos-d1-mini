#include <Wire.h>

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
#include "math.h"
#include "certs.h"
#include "secret.h"

/*
  ==============================================================================
  CONSTANTS & VARIABLES
  ==============================================================================
*/

// SDA and SCL pins for the decibel meter module
#define I2C_SDA                 D2
#define I2C_SCL                 D1

// I2C address for the module
#define DBM_ADDR                0x48

// Device registers
#define   DBM_REG_VERSION       0x00
#define   DBM_REG_ID3           0x01
#define   DBM_REG_ID2           0x02
#define   DBM_REG_ID1           0x03
#define   DBM_REG_ID0           0x04
#define   DBM_REG_SCRATCH       0x05
#define   DBM_REG_CONTROL       0x06
#define   DBM_REG_TAVG_HIGH     0x07
#define   DBM_REG_TAVG_LOW      0x08
#define   DBM_REG_RESET         0x09
#define   DBM_REG_DECIBEL       0x0A
#define   DBM_REG_MIN           0x0B
#define   DBM_REG_MAX           0x0C
#define   DBM_REG_THR_MIN       0x0D
#define   DBM_REG_THR_MAX       0x0E
#define   DBM_REG_HISTORY_0     0x14
#define   DBM_REG_HISTORY_99    0x77

X509List cert(cert_ISRG_Root_X1);

// Use WiFiClientSecure class to create TLS connection
WiFiClientSecure client;

const bool IS_CALIBRATION_MODE = false;

const String REQUEST_PATH = "/ws/put";

const String DEVICE_ID = "nick3"; // With SPL meter hardware
const unsigned long uploadIntervalMS = 60000 * 5; // Upload every 5 mins

uint8_t minReading = 0;
uint8_t maxReading = 0;
unsigned long lastUploadMillis = 0;
bool didInit = false;

/*
  ==============================================================================
  SETUP
  ==============================================================================
*/
void setup() {
  Serial.begin(9600);
  Serial.println();
  
  connectToWifi();
  configTime();

  Serial.print("Connecting to ");
  Serial.println(REQUEST_HOSTNAME);

  Serial.printf("Using certificate: %s\n", cert_ISRG_Root_X1);
  client.setTrustAnchors(&cert);
}

/*
  ==============================================================================
  LOOP
  ==============================================================================
*/
void loop() {

    // Initialize I2C at 10kHz
  Wire.begin (I2C_SDA, I2C_SCL, 10000);

  // Read ID registers
  uint8_t id[4];
  id[0] = dbmeter_readreg (DBM_REG_ID3);
  id[1] = dbmeter_readreg (DBM_REG_ID2);
  id[2] = dbmeter_readreg (DBM_REG_ID1);
  id[3] = dbmeter_readreg (DBM_REG_ID0);
//  Serial.printf ("Unique ID = %02X %02X %02X %02X\r\n", id[3], id[2], id[1], id[0]);

  uint8_t db, dbmin, dbmax;
  // Read decibel, min and max
  db = dbmeter_readreg (DBM_REG_DECIBEL);
  if (db < 255) {
    dbmin = dbmeter_readreg (DBM_REG_MIN);
    dbmax = dbmeter_readreg (DBM_REG_MAX);
  }

  if (!didInit) {
    resetReading(db);
    didInit = true;
  }

  if (IS_CALIBRATION_MODE) {
    displayReading(db, dbmin, dbmax);
  }

  if (db > maxReading) {
    maxReading = db;
  }
  if (db < minReading) {
    minReading = db;
  }
  if (IS_CALIBRATION_MODE) {
    displayReading(db, dbmin, dbmax);
  };

    if (!IS_CALIBRATION_MODE) {
      // If enough time has elapsed since last upload, attempt upload
      long now = millis();
      long msSinceLastUpload = now - lastUploadMillis;
      if (msSinceLastUpload >= uploadIntervalMS) {
        if (!client.connect(REQUEST_HOSTNAME, REQUEST_PORT)) {
          Serial.println("Wifi Client Connection failed");
          return;
        }
        String payload = createJSONPayload();
        uploadData(client, payload);
          long now = millis();
  lastUploadMillis = now;
  resetReading(db);
      };
  };

  delay(100);
};

/*
  ==============================================================================
  FUNCTIONS
  ==============================================================================
*/


uint8_t dbmeter_readreg (uint8_t regaddr)
{
  Wire.beginTransmission (DBM_ADDR);
  Wire.write (regaddr);
  Wire.endTransmission();
  Wire.requestFrom (DBM_ADDR, 1);
  delay (10);
  return Wire.read();
} // Function to read a register from decibel meter

void connectToWifi() {
  Serial.print("Connecting to ");
  Serial.println(NETWORK_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(NETWORK_SSID, NETWORK_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}; // Connect to WiFi network using NETWORK_SSID and NETWORK_PASSWORD

void configTime() {
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}; // Set time via NTP, as required for x.509 validation

String createJSONPayload() {
  // Prepare JSON document
  DynamicJsonDocument doc(2048);
  doc["parent"] = "/Bases/nm1";
  doc["data"]["type"] = "comand";
  doc["data"]["version"] = "1.0";
  doc["data"]["contents"][0]["Type"] = "Noise";
  doc["data"]["contents"][0]["Min"] = minReading;
  doc["data"]["contents"][0]["Max"] = maxReading;
  doc["data"]["contents"][0]["DeviceID"] = DEVICE_ID;

  // Serialize JSON document
  String json;
  serializeJson(doc, json);
  return json;
}; // Assemble JSON payload from global variables

void uploadData(WiFiClientSecure client, String json) {

  Serial.print("Requesting URL: ");
  Serial.println(REQUEST_PATH);

  String request = String("POST ") + REQUEST_PATH + " HTTP/1.1\r\n" +
                   "Host: " + REQUEST_HOSTNAME + "\r\n" +
                   "User-Agent: ESP8266\r\n" +
                   "Connection: close\r\n" +
                   "Authorization: Token " + API_TOKEN + "\r\n" +
                   "Content-Type: application/json\r\n" +
                   "Content-Length: " + json.length() + "\r\n\r\n" +
                   json + "\r\n";

  Serial.println(request);
  client.print(request);

  Serial.println("Request sent");
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("Headers received");
      break;
    }
  }
  String line = client.readString();
  if (line.startsWith("HTTP/1.1 200")) {
    Serial.println("esp8266/Arduino CI successful!");
  } else {
    Serial.println("esp8266/Arduino CI has failed");
  }
  Serial.println("Reply was:");
  Serial.println("==========");
  Serial.println(line);
  Serial.println("==========");
  Serial.println("Closing connection");
}; // Given a serialized JSON payload, upload the data to webcomand

void displayReading(uint8_t db, uint8_t dbmin, uint8_t dbmax) {
  Serial.printf ("dB reading = %03d \t [MIN: %03d \tMAX: %03d] \r\n", db, dbmin, dbmax);
};

void resetReading(uint8_t db) {
  Serial.println("Resetting...");
  Serial.println("Min: " + String(minReading));
  Serial.println("Max: " + String(maxReading));
  minReading = db;
  maxReading = db;
  Serial.println("Reset complete");
  Serial.println("Min: " + String(minReading));
  Serial.println("Max: " + String(maxReading));
};
