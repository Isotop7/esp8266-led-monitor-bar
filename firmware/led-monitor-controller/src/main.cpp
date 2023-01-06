#include "WifiSettings.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>
#include <LittleFS.h>

/*
LED pins
*/
#define RED_LED D5   // GPIO14, D5
#define BLUE_LED D1  // GPIO5, D1
#define GREEN_LED D2 // GPIO4, D2

/*
OLED screen settings
*/
#define OLED_I2C_BUS_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_RESET -1

/*
Wifi settings
*/
const char *ssid = WIFI_SSID;
const char *password = WIFI_PSK;

/*
Filesystem settings
*/
const char *filename = "/boot_config.txt";
boolean readOnlyMode = false;

/*
Global object setup
*/
AsyncWebServer server(80);
CHSV LED_COLOR;
StaticJsonDocument<128> config;
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
String eventQueue;

/*
Icons / sprites
*/
const unsigned ICO_BOOT[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x07, 0xe0, 0x0f,
    0xf0, 0x0d, 0xb0, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
    0x01, 0x80, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/*
    Save settings to filesystem
*/
void saveSettings(const CHSV &hsv) {
  // Don't save settings on read only mode
  if (readOnlyMode) {
    return;
  }
  config["hue"] = hsv.hue;
  config["sat"] = hsv.saturation;
  config["val"] = hsv.value;
  if (hsv.value > 0) {
    config["previousVal"] = hsv.value;
  }

  // Open file, serialize settings and write them to file
  File w = LittleFS.open(filename, "w");
  serializeJson(config, w);
  w.flush();
  w.close();
}

/*
    Outputs the rgb values to the digital pins
*/
void showAnalogRGB(const CHSV &hsv) {
  CRGB rgb;
  hsv2rgb_spectrum(hsv, rgb);
  analogWrite(RED_LED, rgb.r);
  analogWrite(GREEN_LED, rgb.g);
  analogWrite(BLUE_LED, rgb.b);
  saveSettings(hsv);
}

/*
Overwrites oled with string
*/
void writeOLED(String message) {
  oled.setCursor(0, 0);
  oled.clearDisplay();
  oled.display();
  oled.println(message);
  oled.display();
}

/*
    Read settings from filesystem and apply them
*/
void readSettings(String path) {
  // Read boot_config and deserialize
  File r = LittleFS.open(path, "r");
  deserializeJson(config, r);
  r.close();
  Serial.print("+ Found previous settings:");
  Serial.print("hue: " + config["hue"].as<String>());
  Serial.print("; sat: " + config["sat"].as<String>());
  Serial.print("; val: " + config["val"].as<String>());
  Serial.println("; previousVal: " + config["previousVal"].as<String>());
  // Read led config and apply
  LED_COLOR.hue = config["hue"];
  LED_COLOR.saturation = config["sat"];
  LED_COLOR.value = config["val"];
  if (LED_COLOR.value != 0) {
    showAnalogRGB(LED_COLOR);
  }
}

void setup() {
  // Setup console
  Serial.begin(9600);
  Serial.println();
  Serial.println("+ SERIAL | Setup complete");

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_BUS_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    for (;;)
      ;
  }
  Serial.println("+ OLED | OLED setup complete");
  // Set default values for OLED screen and show splash screen
  oled.dim(true);
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(WHITE);
  oled.setCursor(0, 0);

  writeOLED(">esp8266-led-monitor-bar bootup<");

  // Setup wifi
  WiFi.begin(ssid, password);
  Serial.print("+ Connecting to wifi '" + String(ssid) + "' ");
  writeOLED("Connecting to wifi '" + String(ssid) + "' ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  IPAddress ip = WiFi.localIP();
  Serial.println("+ Connected, IP address: " + ip.toString());
  writeOLED("Connected, IP address: " + ip.toString());

  // USE THIS TO FORMAT LittleFS
  // LittleFS.format();

  // Initialize File System
  if (!LittleFS.begin()) {
    Serial.println("+ Error while mounting LittleFS");
    writeOLED("Error while mounting LittleFS");
    readOnlyMode = true;
  } else {
    Serial.println("+ FS mounted. Settings will be saved");
    writeOLED("FS mounted. Settings will be saved");
    readSettings(filename);
  }
  Serial.println("+ Init complete. Listening ...");
  writeOLED("Init complete. Listening ...");

  // Add handlers to server
  // Turn on
  server.on("/api/lightOn", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("+ '/api/lightOn' requested. Turning lights on...");
    eventQueue = "[GET] '/api/lightOn'";
    LED_COLOR.value = config["previousVal"];
    showAnalogRGB(LED_COLOR);
    request->send(200, "text/html", "1");
  });

  // Turn off
  server.on("/api/lightOff", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("+ '/api/lightOff' requested. Turning lights off...");
    eventQueue = "[GET] '/api/lightOff'";
    LED_COLOR.value = 0;
    showAnalogRGB(LED_COLOR);
    request->send(200, "text/html", "0");
  });

  // Get status
  server.on("/api/lightStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("+ '/api/lightStatus' requested. Returning light status...");
    eventQueue = "[GET] '/api/lightStatus'";
    if (LED_COLOR.value == 0) {
      request->send(200, "text/html", "0");
    } else {
      request->send(200, "text/html", "1");
    }
  });

  // Set brightness
  server.on("/api/setBrightness", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("+ '/api/setBrightness' requested.");
    if (request->hasParam("brightness")) {
      AsyncWebParameter *brightness = request->getParam("brightness");
      Serial.println("+ PARAM brightness found: " + brightness->value());
      eventQueue = "[GET] '/api/setBrightness > " + brightness->value();
      LED_COLOR.value = ((brightness->value().toInt() * 255) / 100);
      showAnalogRGB(LED_COLOR);
      request->send(200, "text/html", brightness->value());
    } else {
      request->send(400, "text/plain", "Please specify brightness!");
    }
  });

  // Get brightness
  server.on("/api/getBrightness", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("+ '/api/getBrightness' requested.");
    eventQueue = "[GET] '/api/getBrightness'";
    request->send(200, "text/html", String((LED_COLOR.value / 255) * 100));
  });

  // Set Hue
  server.on("/api/setHue", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("+ '/api/setHue' requested.");
    if (request->hasParam("hue")) {
      AsyncWebParameter *hue = request->getParam("hue");
      Serial.println("+ PARAM hue found: " + hue->value());
      eventQueue = "[GET] '/api/setHue | param: ' " + hue->value();
      LED_COLOR.hue = ((hue->value().toInt() * 182) / 256);
      showAnalogRGB(LED_COLOR);
      request->send(200, "text/html", hue->value());
    } else {
      request->send(400, "text/plain", "Please specify hue!");
    }
  });

  // Get Hue
  server.on("/api/getHue", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("+ '/api/getHue' requested. Returning hue with value ");
    eventQueue = "[GET] '/api/getHue'";
    request->send(200, "text/html", String((LED_COLOR.hue * 256) / 182));
  });

  // Set saturation
  server.on("/api/setSaturation", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("+ '/api/setSaturation' requested.");
    if (request->hasParam("saturation")) {
      AsyncWebParameter *saturation = request->getParam("saturation");
      Serial.println("+ PARAM saturation found: " + saturation->value());
      eventQueue = "[GET] '/api/setSaturation > ' " + saturation->value();
      LED_COLOR.saturation = ((saturation->value().toInt() * 255) / 100);
      showAnalogRGB(LED_COLOR);
      request->send(200, "text/html", saturation->value());
    } else {
      request->send(400, "text/plain", "Please specify hue!");
    }
  });

  // Get saturation
  server.on("/api/getSaturation", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print(
        "+ '/api/getSaturation' requested. Returning saturation with value ");
    eventQueue = "[GET] '/api/getSaturation'";
    request->send(200, "text/html", String((LED_COLOR.saturation / 255) * 100));
  });

  // Default handler for path '/api'
  server.on("/api", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("+ '/api' requested");
    eventQueue = "[GET] '/api'";
    request->send(400, "text/plain", "Please specify endpoint!");
  });

  // Start server loop
  server.begin();
}

void loop() {
  // As we are using the server loop, we can leave this arduino loop definition
  // empty
  if (eventQueue != "") {
    writeOLED(eventQueue);
    eventQueue = "";
  }
}