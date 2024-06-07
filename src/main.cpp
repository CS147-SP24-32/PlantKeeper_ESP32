#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <inttypes.h>
#include <stdio.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ArduinoJson.h"
#include "secrets.h"
#include "FS.h"

#define MOISTURE_PIN 33
#define PUMP_SWITCH_PIN 17
#define MIN_MOISTURE 4095
#define MAX_MOISTURE 1000
#define PUMP_CYCLE_SECONDS 5

const char* ssid = WIFI_SSID;  // define in secrets.h
const char* pass = WIFI_PASS;  // set to NULL for open network
const char* host = LAMBDA_URL;


void setup() {
  Serial.begin(9600);
  pinMode(PUMP_SWITCH_PIN, OUTPUT);
  Serial.println(__FILE__);
  Serial.printf("\nRetrieved MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("Connecting to %s\n", ssid);
  if (pass)
    WiFi.begin(ssid, pass);
  else
    WiFi.begin(ssid);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    Serial.print(WiFi.status());
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("MAC address: ");
    Serial.println(WiFi.macAddress());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

void loop() {
  int moistureValue = analogRead(MOISTURE_PIN);
  int percentMoisture = constrain(map(moistureValue, MAX_MOISTURE, MIN_MOISTURE, 100, 0), 0, 100);
  Serial.printf("\n\nRaw moisture level: %d\n", moistureValue);
  Serial.printf("Adjusted moisture level: %d%%\n", percentMoisture);

  Serial.printf("\n\nSending data to AWS Lambda...\n");
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    String url = url + "?moisture=" + percentMoisture;
    Serial.printf("Connecting to URL: %s", url.c_str());

    http.begin(client, url);
    const int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
      String payload = http.getString();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, http.getStream());
      if (error.code() != ArduinoJson::DeserializationError::Ok) {
        Serial.printf("Failed to deserialize response json: %s\n", error.c_str());
      } else {
        const bool needs_watering = doc["needs_watering"].as<bool>();
        Serial.printf("Message from backend: %s\n", doc["message"].as<std::string>().c_str());
        if (needs_watering) {
          Serial.println("Watering for one second");
          digitalWrite(PUMP_SWITCH_PIN, HIGH);
          sleep(PUMP_CYCLE_SECONDS);
          digitalWrite(PUMP_SWITCH_PIN, LOW);
          Serial.println("Rechecking level in 3 seconds");
          sleep(3);
        } else {
          Serial.printf("60 seconds until next check.");
          sleep(60);
        }
      }
    } else {
      Serial.printf("Error code: ");
      Serial.println(httpResponseCode);
      sleep(10);
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
    sleep(1);
  }
}