// Simple program to periodically download the actual LED state from HTTP endpoint
// based on: https://github.com/jakubcizek/pojdmeprogramovatelektroniku/tree/master/SrazkovyRadar
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>

const char *ssid = "";                  // WiFi SSID
const char *pass = "";                  // WiFi password
const char *url  = "https://services.brewrobot.org/led_map/led_map.json";

uint32_t refreshTimestamp = 0;          // In ms
uint32_t refreshDelay     = 600000;     // In ms
uint32_t reconnectDelay   = 300000;     // In ms
uint8_t  brightness       = 5;          // Base brightness
uint8_t  lastBrightness   = brightness; // Brigtness update check

const uint8_t wifiSignLeds[] = {9, 6, 3, 0, 4, 1, 2, 5, 7, 32, 26, 20, 28, 34, 52, 51};

// 72 RGB LED connected to GPIO pin 25
Adafruit_NeoPixel pixels(72, 25, NEO_GRB + NEO_KHZ800);
// Space for 72 LEDs ('{"id": 255, "r": 255, "g": 255, "b": 255}, ' can take up to 42bytes ~ 3024bytes + array name + brightness)
StaticJsonDocument<8192> pixelsData;

// Fills display with selected color
void dspFillColor(uint8_t r, uint8_t g, uint8_t b) {
    for (size_t pixel_num = 0; pixel_num < pixels.numPixels(); pixel_num++) {
      pixels.setPixelColor(pixel_num, pixels.Color(r, g, b));
    }
    pixels.show();
}

// Displays WiFi sign on the map
void dspWifiSign(uint8_t r, uint8_t g, uint8_t b) {
  for (size_t pixel_num = 0; pixel_num < sizeof(wifiSignLeds); pixel_num++) {
    pixels.setPixelColor(wifiSignLeds[pixel_num], pixels.Color(r, g, b));
  }
  pixels.show();
}

// Test all colors on the display
void displayTest() {
  dspFillColor(255,0,0);
  delay(500);
  dspFillColor(0,255,0);
  delay(500);
  dspFillColor(0,0,255);
  delay(500);
}

// Show ok/error connection message
void displayWiFi(bool ok = true) {
  dspFillColor(255,255,255);
  if (ok)
    dspWifiSign(0,255,0);    
  else
    dspWifiSign(255,0,0);
}

// Decode downloaded JSON and change LEDs in config
int jsonDecoder(String jsonData) {
  DeserializationError e = deserializeJson(pixelsData, jsonData);
  if (e) {
    displayWiFi(false);
    if (e == DeserializationError::InvalidInput) {
      return -1;
    } else if (e == DeserializationError::NoMemory) {
      return -2;
    } else {
      return -3;
    }
  } else {
    pixels.clear();
    JsonArray cities = pixelsData["cities"].as<JsonArray>();
    int brightness = pixelsData["brightness"].as<int>();
    if (lastBrightness != brightness) {
      Serial.println("INFO: setting new brightness");
      pixels.setBrightness(brightness);
      lastBrightness = brightness;
    }
    for (JsonObject city : cities) {
      pixels.setPixelColor(city["id"], pixels.Color(city["r"], city["g"], city["b"]));
    }
    pixels.show();
    return cities.size();
  }
}

// Download and update data
void updateData() {
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int res = jsonDecoder(http.getString());
    if (res < 0) {
      switch (res) {
        case -1:
          Serial.println("ERROR: wrong JSON format");
          break;
        case -2:
          Serial.println("ERROR: not enough memory for JSON, increase JSON document size");
          break;
        case -3:
          Serial.println("ERROR: unknown JSON parse problem");
          break;
      }
    } else {
      Serial.printf("INFO: sucesfully downloaded %d cities\n", res);
    }
  } else {
    displayWiFi(false);
  }
  http.end();
}

// Run!
void setup() {
  // Init serial
  Serial.begin(115200);
  // Init display
  pixels.begin();
  pixels.setBrightness(brightness);
  pixels.clear();
  // Check display
  displayTest();
  displayWiFi(false);
  // Try to connect
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.printf("INFO: connecting to: %s", ssid);
  // If we don't succced on WiFi connection, sleep and restart the process after some time
  int connect_timeout = 15;
  while (WiFi.status() != WL_CONNECTED && connect_timeout > 0) {
    delay(500);
    Serial.print(".");
    connect_timeout--;
  }
  Serial.println("");
  if (connect_timeout > 0) {
    displayWiFi();
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    Serial.printf("INFO: connected to WiFi with IP %s\n", WiFi.localIP().toString());
    // Set DNS
    MDNS.begin("led_map");
    MDNS.addService("http", "tcp", 80);
    // Download actual data
    updateData();
  } else {
    Serial.println("ERROR: not able to connect to WiFi, will go to sleep for later restart");
    esp_sleep_enable_timer_wakeup(reconnectDelay * 1000ULL); // Registrace probuzeni timerem
    esp_deep_sleep_start();
  }
}

void loop() {
  if (millis() - refreshTimestamp > refreshDelay) {
    refreshTimestamp = millis();
    updateData();
  }
  // Check every 1s
  delay(1000);
}
