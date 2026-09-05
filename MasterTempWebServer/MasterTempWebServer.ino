#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include "time.h"

// ----------------------------------------------------
// USER CONFIGURATION: Set your Wi-Fi Credentials here
// ----------------------------------------------------
const char* ssid     = "";
const char* password = "";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;    
const int   daylightOffset_sec = 3600; 

#define I2C_SDA 21
#define I2C_SCL 22

Adafruit_BME680 bme;
WebServer server(80);

const int MAX_UNITS = 10;
const int AVG_SAMPLES = 60; // 60 samples * 5 second updates = 5 minutes history

// Circular buffer structure for a single metric
struct RollingBuffer {
  float samples[AVG_SAMPLES];
  int index = 0;
  int count = 0;

  void add(float val) {
    samples[index] = val;
    index = (index + 1) % AVG_SAMPLES;
    if (count < AVG_SAMPLES) count++;
  }

  float getAverage() {
    if (count == 0) return 0;
    float sum = 0;
    for (int i = 0; i < count; i++) {
      sum += samples[i];
    }
    return sum / count;
  }
};

// Expanded struct to track history per unit
struct RemoteUnit {
  String id;
  float temp;
  float hum;
  float pres;
  float gas;
  String lastSeen;
  bool active = false;
  
  RollingBuffer tempHistory;
  RollingBuffer humHistory;
  RollingBuffer presHistory;
  RollingBuffer gasHistory;
};

// Use dynamically allocated pointers to prevent the static BSS segment from overflowing at boot
RemoteUnit* remoteUnits[MAX_UNITS];
RemoteUnit* hubUnit = nullptr;

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Time Sync Error";
  char timeStringBuff[30]; // FIXED: Added proper absolute width array limits
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

String getUptime() {
  unsigned long totalSeconds = millis() / 1000;
  int days = totalSeconds / 86400;
  totalSeconds %= 86400;
  byte hours = totalSeconds / 3600;
  totalSeconds %= 3600;
  byte minutes = totalSeconds / 60;
  byte seconds = totalSeconds % 60;
  char uptimeBuffer[30]; // FIXED: Added proper absolute width array limits
  snprintf(uptimeBuffer, sizeof(uptimeBuffer), "%dd %02dh %02dm %02ds", days, hours, minutes, seconds);
  return String(uptimeBuffer);
}

void handleUpdate() {
  if (server.hasArg("plain") == false) {
    server.send(400, "text/plain", "Body missing");
    return;
  }

  String body = server.arg("plain");
  String id = "";
  float t=0, h=0, p=0, g=0;
  
  int idIdx = body.indexOf("id=");
  int tIdx = body.indexOf("&temp=");
  int hIdx = body.indexOf("&hum=");
  int pIdx = body.indexOf("&pres=");
  int gIdx = body.indexOf("&gas=");

  if(idIdx != -1 && tIdx != -1) {
    id = body.substring(idIdx + 3, tIdx);
    t = body.substring(tIdx + 6, hIdx).toFloat();
    h = body.substring(hIdx + 5, pIdx).toFloat();
    p = body.substring(pIdx + 6, gIdx).toFloat();
    g = body.substring(gIdx + 5).toFloat();

    int slot = -1;
    for(int i=0; i<MAX_UNITS; i++) {
      if(remoteUnits[i] != nullptr && remoteUnits[i]->active && remoteUnits[i]->id == id) {
        slot = i;
        break;
      }
    }
    if(slot == -1) {
      for(int i=0; i<MAX_UNITS; i++) {
        if(remoteUnits[i] == nullptr) {
          remoteUnits[i] = new RemoteUnit(); // Initialize entry on demand
          slot = i;
          break;
        }
      }
    }

    if(slot != -1) {
      remoteUnits[slot]->id = id;
      remoteUnits[slot]->temp = t;
      remoteUnits[slot]->hum = h;
      remoteUnits[slot]->pres = p;
      remoteUnits[slot]->gas = g;
      remoteUnits[slot]->lastSeen = getFormattedTime();
      remoteUnits[slot]->active = true;

      // Add to running history logs
      remoteUnits[slot]->tempHistory.add(t);
      remoteUnits[slot]->humHistory.add(h);
      remoteUnits[slot]->presHistory.add(p);
      remoteUnits[slot]->gasHistory.add(g);
    }
  }
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

// Reusable UI Card Builder that prints current value and 30min average layout
String buildCard(String label, float current, float average, String unitSign, String color) {
  String cardHtml = "<div class=\"card\">";
  cardHtml += "<h3>" + label + "</h3>";
  cardHtml += "<div class=\"value\" style=\"color: " + color + ";\">" + String(current, 1) + "<span class=\"unit\">" + unitSign + "</span></div>";
  cardHtml += "<div class=\"avg\">5m avg: <b>" + String(average, 1) + "</b>" + unitSign + "</div>";
  cardHtml += "</div>";
  return cardHtml;
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<link rel=\"icon\" href=\"data:,\">";
  html += "<style>html { font-family: Helvetica; margin: 0px auto; text-align: center; background-color: #f7f9fc; color: #333;}";
  html += "body { margin-top: 30px; }";
  html += ".station { background: white; max-width: 850px; margin: 20px auto; padding: 20px; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.08); text-align: left; }";
  html += ".station-title { font-size: 1.4rem; font-weight: bold; border-bottom: 2px solid #34495e; padding-bottom: 5px; color: #2c3e50; margin-bottom: 15px; }";
  html += ".card-container { display: flex; flex-wrap: wrap; justify-content: flex-start; }";
  html += ".card { background: #fdfdfd; padding: 15px; margin: 10px; border-radius: 8px; width: 155px; box-shadow: inset 0 0 4px rgba(0,0,0,0.05); border: 1px solid #eef2f7; text-align: center; }";
  html += ".meta-container { background: #eef2f7; padding: 15px; max-width: 850px; margin: 20px auto; border-radius: 8px; font-size: 0.95rem; text-align: left; }";
  html += "h1 { color: #2c3e50; } .value { font-size: 1.4rem; font-weight: bold; margin-top: 8px; }";
  html += ".unit { font-size: 0.9rem; font-weight: normal; margin-left: 2px; }";
  html += ".avg { font-size: 0.75rem; color: #7f8c8d; margin-top: 6px; border-top: 1px dashed #ddd; padding-top: 4px; }";
  html += ".ts { font-size: 0.8rem; color: #7f8c8d; margin-top: 10px; text-align: right; }</style>";
  html += "<script>setInterval(function() { window.location.reload(); }, 5000);</script>"; 
  html += "</head><body>";
  
  html += "<h1>Multi-Unit Climate Network</h1>";
  
  html += "<div class=\"meta-container\">";
  html += "<strong>Hub Server Date/Time:</strong> " + getFormattedTime() + "<br>";
  html += "<strong>System Uptime:</strong> " + getUptime();
  html += "</div>";
  
  // 1. Render Local Hub Data
  html += "<div class=\"station\">";
  html += "<div class=\"station-title\"> Home Local Hub (BME680 Master)</div>";
  html += "<div class=\"card-container\">";
  html += buildCard("Temp", hubUnit->temp, hubUnit->tempHistory.getAverage(), " &deg;F", "#e74c3c");
  html += buildCard("Humidity", hubUnit->hum, hubUnit->humHistory.getAverage(), " %", "#3498db");
  html += buildCard("Pressure", hubUnit->pres, hubUnit->presHistory.getAverage(), " hPa", "#2ecc71");
  html += buildCard("Gas / VOC", hubUnit->gas, hubUnit->gasHistory.getAverage(), " k&Omega;", "#9b59b6");
  html += "</div></div>";

  // 2. Render Remote Nodes Data
  for(int i=0; i<MAX_UNITS; i++) {
    if(remoteUnits[i] != nullptr && remoteUnits[i]->active) {
      html += "<div class=\"station\">";
      html += "<div class=\"station-title\">📡 Remote Unit: " + remoteUnits[i]->id + "</div>";
      html += "<div class=\"card-container\">";
      html += buildCard("Temp", remoteUnits[i]->temp, remoteUnits[i]->tempHistory.getAverage(), " &deg;F", "#e74c3c");
      html += buildCard("Humidity", remoteUnits[i]->hum, remoteUnits[i]->humHistory.getAverage(), " %", "#3498db");
      html += buildCard("Pressure", remoteUnits[i]->pres, remoteUnits[i]->presHistory.getAverage(), " hPa", "#2ecc71");
      html += buildCard("Gas / VOC", remoteUnits[i]->gas, remoteUnits[i]->gasHistory.getAverage(), " k&Omega;", "#9b59b6");
      html += "</div><div class=\"ts\">Last updated: " + remoteUnits[i]->lastSeen + "</div></div>";
    }
  }

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Initialize heap tracking assignments
  hubUnit = new RemoteUnit();
  for(int i=0; i<MAX_UNITS; i++) {
    remoteUnits[i] = nullptr;
  }

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!bme.begin(0x77)) {
    if (!bme.begin(0x76)) {
      Serial.println(F("Error: Could not find a valid BME680 sensor!"));
      while (1);
    }
  }

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);

  Serial.println("");
  Serial.print("Connecting to Wi-Fi network: ");
  Serial.println(ssid);
  
  WiFi.disconnect(); 
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    if (attempts > 30) { 
      Serial.println("\n[ERROR] Connection taking too long!");
      Serial.print("Current Status Code: ");
      Serial.println(WiFi.status()); 
      Serial.println("Please check your SSID/Password spelling.");
      attempts = 0; 
    }
  }

  Serial.println("");
  Serial.println("==================================================");
  Serial.println(" STATUS: Wi-Fi connected successfully!");
  Serial.print(" LOCAL IP ADDRESS: ");
  Serial.println(WiFi.localIP());
  Serial.print(" ACCESS SERVER HERE: http://"); 
  Serial.println(WiFi.localIP()); 
  Serial.println("==================================================");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Synchronizing clock registers via NTP...");

  server.on("/", handleRoot);
  server.on("/update", HTTP_POST, handleUpdate); 
  server.begin();
  Serial.println("HTTP Web Server operational.");
}

void loop() {
  server.handleClient();

  // Log the host internal sensor metrics every 5 seconds to track rolling averages
  static unsigned long lastHubLog = 0;
  if (millis() - lastHubLog >= 5000) {
    lastHubLog = millis();
    if (bme.performReading()) {
      hubUnit->temp = (bme.temperature * 9.0 / 5.0) + 32.0;
      hubUnit->hum = bme.humidity;
      hubUnit->pres = bme.pressure / 100.0;
      hubUnit->gas = bme.gas_resistance / 1000.0;

      hubUnit->tempHistory.add(hubUnit->temp);
      hubUnit->humHistory.add(hubUnit->hum);
      hubUnit->presHistory.add(hubUnit->pres);
      hubUnit->gasHistory.add(hubUnit->gas);
    }
  }
}

