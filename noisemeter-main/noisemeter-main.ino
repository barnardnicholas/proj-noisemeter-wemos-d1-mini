#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
#include "math.h"
#include "certs.h"
#include "secret.h"

/*
==============================================================================
CONSTANTS
==============================================================================
*/
X509List cert(cert_ISRG_Root_X1);

String REQUEST_PATH = "/ws/put";

/*
==============================================================================
SETUP
==============================================================================
*/
void setup() {
  connectToWifi();
  configTime();

  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;
  Serial.print("Connecting to ");
  Serial.println(REQUEST_HOSTNAME);

  Serial.printf("Using certificate: %s\n", cert_ISRG_Root_X1);
  client.setTrustAnchors(&cert);

  if (!client.connect(REQUEST_HOSTNAME, REQUEST_PORT)) {
    Serial.println("Wifi Client Connection failed");
    return;
  }

  String payload = createJSONPayload();

  uploadData(client, payload);

}

/*
==============================================================================
LOOP
==============================================================================
*/
void loop() {}

/*
==============================================================================
FUNCTIONS
==============================================================================
*/
void connectToWifi() {
  Serial.begin(9600);
  Serial.println();
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
    doc["data"]["contents"][0]["Min"] = 0;
    doc["data"]["contents"][0]["Max"] = 70;
    doc["data"]["contents"][0]["DeviceID"] = "nick2";

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
