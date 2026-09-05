#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"

// ----------------------------------------------------
// USER CONFIGURATION
// ----------------------------------------------------
const char* ssid     = "Hacknet";     
const char* password = "Dylan80r123"; 

// PUT YOUR ACTUAL HUB IP ADDRESS HERE (with /update at the end)
const char* hubAddress = "http://192.168.1.222/update"; 

// Unique ID for this remote unit
const String nodeID = "Room 2"; 

#define I2C_SDA 21
#define I2C_SCL 22

// CRITICAL CONFIG: Built-in LED pin (usually pin 2 on ESP32 DevModules)
#define LED_PIN 2

Adafruit_BME680 bme;

unsigned long lastSendTime = 0;
const unsigned long sendInterval = 30000; // 30 seconds

// LED Timing variables for non-blocking illumination
unsigned long ledTurnOffTime = 0;
bool ledActive = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Configure LED Pin as an output
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Start with LED turned off

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!bme.begin()) {
    Serial.println("Could not find a valid BME680 sensor, check wiring!");
    while(1); 
  }

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); 
  Serial.println("Room 2 BME680 Initialized.");

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected successfully!");
}

void loop() {
  // 1. Check if the 1-second LED window has passed (Non-blocking)
  if (ledActive && (millis() >= ledTurnOffTime)) {
    digitalWrite(LED_PIN, LOW);
    ledActive = false;
    Serial.println("LED turned off.");
  }

  // 2. Main 30-second transmission interval ticker loop
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();

    if (WiFi.status() == WL_CONNECTED) {
      if (!bme.performReading()) {
        Serial.println("Failed to perform BME680 reading :(");
        return;
      }

      float tempF = (bme.temperature * 1.8) + 32.0; 
      float hum   = bme.humidity;
      float pres  = bme.pressure / 100.0;           
      float gas   = bme.gas_resistance / 1000.0;    

      String payload = "id="   + nodeID +
                       "&temp=" + String(tempF, 2) +
                       "&hum="  + String(hum, 2) +
                       "&pres=" + String(pres, 2) +
                       "&gas="  + String(gas, 2);

      Serial.println("\nSending data for Room 2: " + payload);

      // FIXED: Turn on the LED and set its 1-second turn-off timestamp
      digitalWrite(LED_PIN, HIGH);
      ledActive = true;
      ledTurnOffTime = millis() + 1000; 

      HTTPClient http;
      http.begin(hubAddress);
      
      // FIXED: Reverted header to plain text layout to match your original Hub parser
      http.addHeader("Content-Type", "text/plain");

      int httpResponseCode = http.POST(payload);

      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.print("Hub Response: ");
        Serial.println(response);
      } else {
        Serial.print("Transmission error: ");
        Serial.println(httpResponseCode);
      }
      http.end();
    } else {
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
}
