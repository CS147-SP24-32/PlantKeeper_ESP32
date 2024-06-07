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

#define MOISTURE_PIN GPIO_NUM_33
#define PHOTORESISTOR_PIN GPIO_NUM_32
#define END_CALIBRATION_BOTTON_PIN GPIO_NUM_0
#define PUMP_SWITCH_PIN GPIO_NUM_17
#define WATERING_CYCLE_SECONDS 5
#define PWM_CYCLE_MS 100
#define PWM_DUTY_CYCLE 0.20
#define CALIBRATION_SAMPLE_RATE 10
#define SLEEP_AFTER_WATERING_S 10

const char* ssid = WIFI_SSID;  // define in secrets.h
const char* pass = WIFI_PASS;  // set to NULL for open network
const char* statusUrl = LAMBDA_URL;
bool calibrationFinished = false;
int min_moisture = 0, max_moisture = 4095; // (lower value = higher moisture lvl)
int min_light = 4095, max_light = 0;

void setup() {
  Serial.begin(9600);
  pinMode(PUMP_SWITCH_PIN, OUTPUT);
  digitalWrite(PUMP_SWITCH_PIN, LOW);
  pinMode(PHOTORESISTOR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  Serial.println(__FILE__);

  // attempt wifi connection
  Serial.printf("\nMAC address: %s\n", WiFi.macAddress().c_str());
  Serial.printf("Connecting to %s\n", ssid);
  if (pass) WiFi.begin(ssid, pass);
  else WiFi.begin(ssid);
  for (int attempts = 0; attempts < 20 && WiFi.status() != WL_CONNECTED; ++attempts) {
    delay(500);
    Serial.printf(".%d", WiFi.status());
  }
  if (WiFi.status() == WL_CONNECTED) Serial.printf("\nConnected; IP address: %s\n", WiFi.localIP().toString().c_str());
  else {
    Serial.println("\nFailed to connect to WiFi");
    return;
  }

  // calibration
  Serial.printf("Sensor calibration started; press pin %d to end.\n", END_CALIBRATION_BOTTON_PIN);
  attachInterrupt(END_CALIBRATION_BOTTON_PIN, []() {calibrationFinished = true;}, RISING);
  while (!calibrationFinished) {
    int moistureValue = analogRead(MOISTURE_PIN);
    int ll = analogRead(PHOTORESISTOR_PIN);
    if (moistureValue < max_moisture) max_moisture = moistureValue;
    if (moistureValue > min_moisture) min_moisture = moistureValue;
    if (ll < min_light) min_light = ll;
    if (ll > max_light) max_light = ll;
    delay(1.0 / CALIBRATION_SAMPLE_RATE * 1000);
  }
  detachInterrupt(END_CALIBRATION_BOTTON_PIN);
  Serial.printf("Calibrated moisture range: %d-%d\n", max_moisture, min_moisture);
  Serial.printf("Calibrated light range: %d-%d\n", min_light, max_light);
  // reject if (min_moisture < 3000 || max_moisture > 2000)
}

void loop() {
  int moistureValue = analogRead(MOISTURE_PIN);
  int lightValue = analogRead(PHOTORESISTOR_PIN);
  int percentMoisture = constrain(map(moistureValue, max_moisture, min_moisture, 100, 0), 0, 100);
  int percentLight = constrain(map(lightValue, min_light, max_light, 0, 100), 0, 100);
  Serial.printf("\n\nRaw moisture level: %d\n", moistureValue);
  Serial.printf("Adjusted moisture level: %d%%\n", percentMoisture);
  Serial.printf("Raw light level: %d\n", lightValue);
  Serial.printf("Adjusted light level: %d%%\n", percentLight);
  Serial.printf("\n\nSending data to AWS Lambda...\n");
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    JsonDocument requestData;
    requestData["moisture"] = percentMoisture;
    requestData["light"] = percentLight;
    String httpBody;
    serializeJson(requestData, httpBody);
    Serial.printf("Connecting to URL: %s", statusUrl);
    http.begin(client, statusUrl);

    const int httpResponseCode = http.POST(httpBody);

    if (httpResponseCode == 200) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, http.getStream());
      if (error.code() != ArduinoJson::DeserializationError::Ok) {
        Serial.printf("Failed to deserialize response json: %s\n", error.c_str());
      } else {
        const bool needs_watering = doc["needs_watering"].as<bool>();
        Serial.printf("Message from backend: %s\n", doc["message"].as<std::string>().c_str());
        if (needs_watering) {
          Serial.printf("Watering for %d second(s)", WATERING_CYCLE_SECONDS);
          for (int i = 0; i < WATERING_CYCLE_SECONDS * 1000 / PWM_CYCLE_MS; i++) {
            digitalWrite(PUMP_SWITCH_PIN, HIGH);
            delay(PWM_CYCLE_MS * PWM_DUTY_CYCLE);
            digitalWrite(PUMP_SWITCH_PIN, LOW);
            delay(PWM_CYCLE_MS * (1-PWM_DUTY_CYCLE));
          }
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