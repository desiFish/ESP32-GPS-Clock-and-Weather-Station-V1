/*
 * GPS-CLOCK-V1.ino
 *
 * Note: An enhanced version of this project with buttons for better device configuration
 * is available at: https://github.com/desiFish/GPS-CLOCK-V2
 *
 * Copyright (C) 2024 desiFish

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Pin Configuration:
 * LCD:
 *   - LCD Brightness: GPIO4 (PWM)
 *   - LCD Enable: GPIO33
 *   - LCD SPI: GPIO18 (CLK), GPIO23 (MOSI), GPIO5 (CS)
 *
 * Sensors:
 *   - BME280: I2C (SDA: GPIO21, SCL: GPIO22)
 *   - BH1750: I2C (shares bus with BME280)
 *
 * GPS:
 *   - RX: GPIO16
 *   - TX: GPIO17
 *   - Power Control: GPIO19
 *
 * Buzzer:
 *   - Control: GPIO25

 * Sketch uses 1099226 bytes (83%) of program storage space. Maximum is 1310720 bytes.
 * Global variables use 49520 bytes (15%) of dynamic memory, leaving 278160 bytes for local variables. Maximum is 327680 bytes.
 */

#include <SPI.h>
#include <TinyGPSPlus.h>
#include <U8g2lib.h>

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>

#include <ArduinoJson.h>

#include <time.h>
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
// --- Web server and filesystem includes ---
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
// Data Storage
#include <Preferences.h>
// Preference library object or instance
Preferences pref;

// Software version
#define SWVersion "v2.0.0"

// --- Async Web Server ---
AsyncWebServer server(80);

// GPS things
static const int RXPin = 16, TXPin = 17; // GPS UART pins
TinyGPSPlus gps;
#define gpsPin 19        // GPS power control pin
#define wifiButtonPin 26 // WiFi toggle button pin

static bool wifiEnabled; // WiFi state variable (initialized from preferences)

// --- Config state variables ---
static bool buzzerOn = false;
static bool displayOffInDark = false;
// --- Config file paths ---
const char *BUZZER_FILE = "/buzzer.json";
const char *DISPLAY_DARK_FILE = "/display_dark.json";

// Time related
time_t currentEpoch;
struct tm timeinfo;

// --- Config load/save helpers ---
void loadConfig()
{
  // Buzzer
  if (LittleFS.exists(BUZZER_FILE))
  {
    File f = LittleFS.open(BUZZER_FILE, "r");
    if (f)
    {
      DynamicJsonDocument doc(64);
      if (deserializeJson(doc, f) == DeserializationError::Ok && doc["on"].is<bool>())
        buzzerOn = doc["on"];
      f.close();
    }
  }
  // Display Off in Dark
  if (LittleFS.exists(DISPLAY_DARK_FILE))
  {
    File f = LittleFS.open(DISPLAY_DARK_FILE, "r");
    if (f)
    {
      DynamicJsonDocument doc(64);
      if (deserializeJson(doc, f) == DeserializationError::Ok && doc["enabled"].is<bool>())
        displayOffInDark = doc["enabled"];
      f.close();
    }
  }
}

void saveBuzzerConfig()
{
  File f = LittleFS.open(BUZZER_FILE, "w");
  if (f)
  {
    DynamicJsonDocument doc(16);
    doc["on"] = buzzerOn;
    serializeJson(doc, f);
    f.close();
  }
}
void saveDisplayDarkConfig()
{
  File f = LittleFS.open(DISPLAY_DARK_FILE, "w");
  if (f)
  {
    DynamicJsonDocument doc(16);
    doc["enabled"] = displayOffInDark;
    serializeJson(doc, f);
    f.close();
  }
}

// Web server setup function
void setupWebServer()
{
  // Serve static files from LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Add a handler for root path
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (LittleFS.exists("/index.html")) {
      request->send(LittleFS, "/index.html", "text/html");
    } else {
      request->send(404, "text/plain", "File not found! Please check if files are uploaded to LittleFS");
    } });

  // Status endpoint
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "OK"); });

  // --- Buzzer API (GET/POST, persistent) ---
  server.on("/api/buzzer", HTTP_GET, [](AsyncWebServerRequest *request)
            {
      DynamicJsonDocument doc(16);
      doc["on"] = buzzerOn;
      String out; serializeJson(doc, out);
      request->send(200, "application/json", out); });
  server.on("/api/buzzer", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              // Handle the request completion
            },
            NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      if (len > 0) {
        DynamicJsonDocument doc(32);
        if (deserializeJson(doc, (const char*)data, len) == DeserializationError::Ok && doc["on"].is<bool>()) {
          buzzerOn = doc["on"];
          //digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);
          saveBuzzerConfig();
          DynamicJsonDocument resp(16);
          resp["on"] = buzzerOn;
          String out; serializeJson(resp, out);
          request->send(200, "application/json", out);
        } else {
          request->send(400, "application/json", "{\"error\":\"Invalid data\"}");
        }
      } else {
        request->send(400, "application/json", "{\"error\":\"No body\"}");
      } });

  // --- Display Off in Dark API (GET/POST, persistent) ---
  server.on("/api/display/off-in-dark", HTTP_GET, [](AsyncWebServerRequest *request)
            {
      DynamicJsonDocument doc(16);
      doc["enabled"] = displayOffInDark;
      String out; serializeJson(doc, out);
      request->send(200, "application/json", out); });
  server.on("/api/display/off-in-dark", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              // Handle the request completion
            },
            NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      if (len > 0) {
        DynamicJsonDocument doc(32);
        if (deserializeJson(doc, (const char*)data, len) == DeserializationError::Ok && doc["enabled"].is<bool>()) {
          displayOffInDark = doc["enabled"];
          saveDisplayDarkConfig();
          DynamicJsonDocument resp(16);
          resp["enabled"] = displayOffInDark;
          String out; serializeJson(resp, out);
          request->send(200, "application/json", out);
        } else {
          request->send(400, "application/json", "{\"error\":\"Invalid data\"}");
        }
      } else {
        request->send(400, "application/json", "{\"error\":\"No body\"}");
      } });

  // --- GPS Info endpoint ---
  server.on("/api/gps", HTTP_GET, [](AsyncWebServerRequest *request)
            {
      DynamicJsonDocument doc(128);
      doc["satellites"] = gps.satellites.value();
      doc["lat"] = gps.location.isValid() ? gps.location.lat() : JsonVariant().set(nullptr);
      doc["lng"] = gps.location.isValid() ? gps.location.lng() : JsonVariant().set(nullptr);
      doc["hdop"] = gps.hdop.isValid() ? gps.hdop.hdop() : JsonVariant().set(nullptr);
      doc["alt"] = gps.altitude.isValid() ? gps.altitude.meters() : JsonVariant().set(nullptr);
      String out; serializeJson(doc, out);
      request->send(200, "application/json", out); });

  // --- Version endpoint ---
  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", SWVersion); });

  // --- System Details endpoint ---
  server.on("/api/system/details", HTTP_GET, [](AsyncWebServerRequest *request)
            {
      DynamicJsonDocument doc(256);
      doc["device"] = "ESP32";
      doc["chipModel"] = ESP.getChipModel();
      doc["chipRevision"] = ESP.getChipRevision();
      doc["chipId"] = (uint32_t)ESP.getEfuseMac();
      doc["macAddress"] = WiFi.macAddress();
      doc["flashSize"] = ESP.getFlashChipSize();
      doc["flashSpeed"] = ESP.getFlashChipSpeed();
      doc["freeHeap"] = ESP.getFreeHeap();
      doc["sketchSize"] = ESP.getSketchSize();
      doc["cpuFreqMHz"] = getCpuFrequencyMhz();
      doc["wifiRssi"] = WiFi.RSSI();
      doc["ip"] = WiFi.localIP().toString();
      doc["sdkVersion"] = ESP.getSdkVersion();
      doc["coreVersion"] = ESP.getCoreVersion();
      String out; serializeJson(doc, out);
      request->send(200, "application/json", out); });
}

// your wifi name and password (saved in preference library)
String ssid;
String password;

/**
 * @brief WiFi Manager HTML template
 * Contains responsive design for configuration portal
 * Styles included for better mobile experience
 */
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Wi-Fi Manager</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
html {
  font-family: Arial, Helvetica, sans-serif; 
  display: inline-block; 
  text-align: center;
}

h1 {
  font-size: 1.8rem; 
  color: white;
}

p { 
  font-size: 1.4rem;
}

.topnav { 
  overflow: hidden; 
  background-color: #0A1128;
}

body {  
  margin: 0;
}

.content { 
  padding: 5%;
}

.card-grid { 
  max-width: 800px; 
  margin: 0 auto; 
  display: grid; 
  grid-gap: 2rem; 
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
}

.card { 
  background-color: white; 
  box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);
}

.card-title { 
  font-size: 1.2rem;
  font-weight: bold;
  color: #034078
}

input[type=submit] {
  border: none;
  color: #FEFCFB;
  background-color: #034078;
  padding: 15px 15px;
  text-align: center;
  text-decoration: none;
  display: inline-block;
  font-size: 16px;
  width: 100px;
  margin-right: 10px;
  border-radius: 4px;
  transition-duration: 0.4s;
  }

input[type=submit]:hover {
  background-color: #1282A2;
}

input[type=text], input[type=number], select {
  width: 50%;
  padding: 12px 20px;
  margin: 18px;
  display: inline-block;
  border: 1px solid #ccc;
  border-radius: 4px;
  box-sizing: border-box;
}

label {
  font-size: 1.2rem; 
}
.value{
  font-size: 1.2rem;
  color: #1282A2;  
}
.state {
  font-size: 1.2rem;
  color: #1282A2;
}
button {
  border: none;
  color: #FEFCFB;
  padding: 15px 32px;
  text-align: center;
  font-size: 16px;
  width: 100px;
  border-radius: 4px;
  transition-duration: 0.4s;
}
.button-on {
  background-color: #034078;
}
.button-on:hover {
  background-color: #1282A2;
}
.button-off {
  background-color: #858585;
}
.button-off:hover {
  background-color: #252524;
} 
  </style>
</head>
<body>
  <div class="topnav">
    <h1>Wi-Fi Manager</h1>
  </div>
  <div class="content">
    <div class="card-grid">
      <div class="card">
        <form action="/wifi" method="POST">
          <p>
            <label for="ssid">SSID</label>
            <input type="text" id ="ssid" name="ssid"><br>
            <label for="pass">Password</label>
            <input type="text" id ="pass" name="pass"><br>
            <input type ="submit" value ="Submit">
          </p>
        </form>
      </div>
    </div>
  </div>
</body>
</html>
)rawliteral";

// Search for parameter in HTTP POST request
const char *PARAM_INPUT_1 = "ssid";
const char *PARAM_INPUT_2 = "pass";

const String newHostname = "NiniClock"; // any name that you desire

// Elegant OTA related task
bool updateInProgress = false;
unsigned long ota_progress_millis = 0;

Adafruit_BME280 bme; // object environmental sensor
float temperature, humidity, pressure;
BH1750 lightMeter; // object light sensor

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, 18, 23, 5, U8X8_PIN_NONE);

#define LCD_LIGHT 4     // PWM pin for LCD backlight control
#define lcdEnablePin 33 // LCD enable pin
#define BUZZER_PIN 25   // Buzzer control pin

char week[7][12] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char monthChar[12][12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// variables for holding time data globally
byte days = 0, months = 0, hours = 0, minutes = 0, seconds = 0;
int years = 0;

bool isDark, hour12Mode = true; // Tracks ambient light state

// LUX (BH1750) update frequency
unsigned long lastLightRead = 0;
unsigned long lastBrightnessUpdate = 0;

const unsigned long lightInterval = 4000;    // lux sampling
const unsigned long brightnessInterval = 50; // animation

// BME280 update frequency
unsigned long lastTime2 = 0;    // Last temperature sensor update
const long timerDelay2 = 12000; // Temperature update interval (1 minute)

time_t prevDisplay = 0; // when the digital clock was displayed

// for creating task attached to CORE 0 of CPU
TaskHandle_t loop1Task;

// Add this with other global variables
byte currentBrightness = 250; // Track current brightness level

const int pwmChannel = 0;    // PWM channel 0–15
const int pwmFreq = 5000;    // PWM frequency in Hz
const int pwmResolution = 8; // 8-bit resolution (0–255)

const int BUZZER_CHANNEL = 1;
const int PWM_FREQ = 2700; // default tone frequency
const int PWM_RES = 8;     // 8-bit resolution

/**
 * @brief Callback when OTA update starts
 * Shows update status on LCD display
 */
void onOTAStart()
{
  // Log when OTA has started
  updateInProgress = true;
  Serial.println("OTA update started!");
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_luRS08_tr);
  u8g2.setCursor(1, 20);
  u8g2.print("OTA UPDATE");
  u8g2.setCursor(1, 32);
  u8g2.print("HAVE STARTED");
  u8g2.sendBuffer();
  delay(1000);
  // <Add your own code here>
}

/**
 * @brief Progress callback during OTA update
 * @param current Number of bytes transferred
 * @param final Total number of bytes
 * Shows progress on LCD with byte counts
 */
void onOTAProgress(size_t current, size_t final)
{
  // Log
  if (millis() - ota_progress_millis > 500)
  {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_luRS08_tr);
    u8g2.setCursor(1, 20);
    u8g2.print("OTA UPDATE");
    u8g2.setCursor(1, 32);
    u8g2.print("UNDER PROGRESS");
    u8g2.setCursor(1, 44);
    u8g2.print("Done: ");
    u8g2.print(current);
    u8g2.print(" bytes");
    u8g2.setCursor(1, 56);
    u8g2.print("Total: ");
    u8g2.print(final);
    u8g2.print(" bytes");
    u8g2.sendBuffer();
  }
}

/**
 * @brief Callback when OTA update ends
 * @param success Boolean indicating if update was successful
 * Shows completion status on LCD display
 */
void onOTAEnd(bool success)
{
  // Log when OTA has finished
  if (success)
  {
    Serial.println("OTA update finished successfully!");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_luRS08_tr);
    u8g2.setCursor(1, 20);
    u8g2.print("OTA UPDATE");
    u8g2.setCursor(1, 32);
    u8g2.print("COMPLETED!");
    u8g2.sendBuffer();
  }
  else
  {
    Serial.println("There was an error during OTA update!");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_luRS08_tr);
    u8g2.setCursor(1, 20);
    u8g2.print("OTA UPDATE");
    u8g2.setCursor(1, 32);
    u8g2.print("HAVE FAILED");
    u8g2.sendBuffer();
  }

  updateInProgress = false;
  delay(1000);
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Start");
  Wire.begin();

  // Initialize WiFi button pin
  pinMode(wifiButtonPin, INPUT);
  pinMode(gpsPin, OUTPUT);
  digitalWrite(gpsPin, HIGH); // you can connect the gps directly to 3.3V pin
  pinMode(LCD_LIGHT, OUTPUT);
  pinMode(lcdEnablePin, OUTPUT);
  digitalWrite(lcdEnablePin, HIGH);
  pinMode(BUZZER_PIN, OUTPUT);

  // Attach buzzer to PWM channel
  ledcAttachChannel(BUZZER_PIN, PWM_FREQ, PWM_RES, BUZZER_CHANNEL);
  // Setup ESP32 PWM for backlight
  ledcAttachChannel(LCD_LIGHT, pwmFreq, pwmResolution, pwmChannel);
  ledcWrite(LCD_LIGHT, 150);

  Serial1.begin(9600, SERIAL_8N1, RXPin, TXPin);

  ledcWrite(LCD_LIGHT, 250);
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.drawLine(0, 17, 127, 17);
  u8g2.setFont(u8g2_font_7x14B_mr);
  u8g2.setCursor(12, 30);
  u8g2.print("GPS Clock V1");
  u8g2.drawLine(0, 31, 127, 31);
  u8g2.sendBuffer();

  if (!lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE))
  {
    errorMsgPrint("BH1750", "CANNOT FIND");
    delay(100); // Wait between sensor inits
  }

  if (!pref.begin("database", false))
  { // open database
    errorMsgPrint("DATABASE", "NOT WORKING");
    delay(100); // Wait between inits
  }

  // Restore WiFi state from preferences (default to true if not set)
  wifiEnabled = pref.getBool("wifi_enabled", true);

  if (!bme.begin(BME280_ADDRESS_ALTERNATE))
  {
    errorMsgPrint("BME280", "CANNOT FIND");
  }

  // Set up oversampling and filter initialization
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X16,     // temperature
                  Adafruit_BME280::SAMPLING_X16,     // pressure
                  Adafruit_BME280::SAMPLING_X16,     // humidity
                  Adafruit_BME280::FILTER_X16,       // filter
                  Adafruit_BME280::STANDBY_MS_1000); // set delay between measurements

  // wifi manager
  // --- LittleFS Init ---
  if (!LittleFS.begin())
  {
    Serial.println("[ERROR] LittleFS Mount Failed");
    Serial.println("Formatting LittleFS...");
    if (LittleFS.format())
    {
      Serial.println("LittleFS formatted successfully");
      if (!LittleFS.begin())
      {
        Serial.println("[ERROR] LittleFS Mount Failed even after formatting");
        while (1)
        {
          delay(1000); // Prevent watchdog reset
        }
      }
    }
    else
    {
      Serial.println("[ERROR] LittleFS Format Failed");
      while (1)
      {
        delay(1000); // Prevent watchdog reset
      }
    }
  }
  Serial.println("LittleFS mounted successfully!");

  // Debug - List all files in LittleFS
  Serial.println("\nListing files in LittleFS root:");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    String fileName = file.name();
    size_t fileSize = file.size();
    Serial.printf(" - File: %s, Size: %u bytes\n", fileName.c_str(), fileSize);
    file = root.openNextFile();
  }

  loadConfig(); // <-- Load config from LittleFS before anything else

  if (wifiEnabled)
  {
    bool wifiConfigExist = pref.isKey("ssid");
    if (!wifiConfigExist)
    {
      pref.putString("ssid", "");
      pref.putString("password", "");
    }

    ssid = pref.getString("ssid", "");
    password = pref.getString("password", "");

    if (ssid == "" || password == "")
    {
      Serial.println("No values saved for ssid or password");
      // Connect to Wi-Fi network with SSID and password
      Serial.println("Setting AP (Access Point)");
      // NULL sets an open Access Point
      WiFi.softAP("WIFI_MANAGER", "WIFImanager");

      IPAddress IP = WiFi.softAPIP();
      Serial.print("AP IP address: ");
      Serial.println(IP);
      wifiManagerInfoPrint();

      // Web Server Root URL
      server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(200, "text/html", index_html); });

      server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest *request)
                {
        int params = request->params();
        for (int i = 0; i < params; i++) {
          const AsyncWebParameter* p = request->getParam(i);
          if (p->isPost()) {
            // HTTP POST ssid value
            if (p->name() == PARAM_INPUT_1) {
              ssid = p->value();
              Serial.print("SSID set to: ");
              Serial.println(ssid);
              ssid.trim();
              pref.putString("ssid", ssid);
            }
            // HTTP POST pass value
            if (p->name() == PARAM_INPUT_2) {
              password = p->value();
              Serial.print("Password set to: ");
              Serial.println(password);
              password.trim();
              pref.putString("password", password);
            }
            //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
          }
        }
        request->send(200, "text/plain", "Done. Device will now restart.");
        delay(3000);
        ESP.restart(); });
      server.begin();
      WiFi.onEvent(WiFiEvent);
      while (true)
      {
        delay(50);
        if (analogRead(wifiButtonPin) > 1000)
        {
          errorMsgPrint("WIFI", "CANCELLED");
          delay(100);
          server.end();
          WiFi.softAPdisconnect(true);
          break; // exit loop to continue normal flow
        }
      }
    }

    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname("GPSClockV1");
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.println("");

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_luRS08_tr);
    u8g2.setCursor(1, 20);
    u8g2.print("WAITING FOR WIFI");
    u8g2.setCursor(1, 32);
    u8g2.print("TO CONNECT");
    u8g2.sendBuffer();

    // count variable stores the status of WiFi connection. 0 means NOT CONNECTED. 1 means CONNECTED

    bool count = true;
    while (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_luRS08_tr);
      u8g2.setCursor(1, 20);
      u8g2.print("COULD NOT CONNECT");
      u8g2.setCursor(1, 32);
      u8g2.print("CHECK CONNECTION");
      u8g2.setCursor(1, 44);
      u8g2.print("OR, RESET AND");
      u8g2.setCursor(1, 56);
      u8g2.print("TRY AGAIN");
      u8g2.sendBuffer();
      Serial.println("Connection Failed");
      delay(6000);
      count = false;
      break;
    }
    if (count)
    { // if wifi is connected
      Serial.println(ssid);
      Serial.println(WiFi.localIP());
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_luRS08_tr);
      u8g2.setCursor(1, 20);
      u8g2.print("WIFI CONNECTED");
      u8g2.setCursor(1, 42);
      u8g2.print(WiFi.localIP());
      u8g2.sendBuffer();

      // Setup web server routes first
      setupWebServer(); // Setup all web server routes and handlers

      Serial.println("HTTP server started");

      // Setup OTA after routes
      ElegantOTA.begin(&server); // Start ElegantOTA
      // ElegantOTA callbacks
      ElegantOTA.onStart(onOTAStart);
      ElegantOTA.onProgress(onOTAProgress);
      ElegantOTA.onEnd(onOTAEnd);

      // Start server last after all routes are set
      server.begin();
      Serial.println("HTTP server started");
      delay(3500);
    }
  }
  pref.end();
  // Wifi related stuff END

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_mr);
  u8g2.setCursor(44, 30);
  u8g2.print("HELLO!");
  u8g2.setCursor(36, 52);
  u8g2.print("NINI");
  u8g2.setFont(u8g2_font_streamline_food_drink_t);
  u8g2.drawUTF8(80, 54, "U+4"); // birthday cake icon
  u8g2.sendBuffer();
  delay(2000);
  xTaskCreatePinnedToCore(
      loop1,       // Task function.
      "loop1Task", // name of task.
      10000,       // Stack size of task
      NULL,        // parameter of the task
      1,           // priority of the task
      &loop1Task,  // Task handle to keep track of created task
      0);          // pin task to core 0
}
float lux = 0;
byte targetBrightness = 0;

/**
 * @brief Secondary loop running on Core 0
 *
 * Responsibilities:
 * - Periodically read the light sensor (BH1750) and update ambient light state.
 * - Adjust LCD brightness automatically and smoothly if enabled.
 * - Read temperature and humidity from the AHT sensor at intervals.
 * - Trigger alarms (hourly/half-hourly) and buzzer as needed.
 * - Perform power-saving operations based on ambient light and settings.
 *
 * @param pvParameters Required by FreeRTOS
 */
void loop1(void *pvParameters)
{
  for (;;)
  {
    if (millis() - lastLightRead > lightInterval)
    {
      lastLightRead = millis();

      lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE);

      // Block until measurement ready (with timeout for safety)
      unsigned long start = millis();
      while (!lightMeter.measurementReady(true))
      {
        if (millis() - start > 3000)
        { // 3s timeout
          Serial.println("[ERROR] Light sensor timeout!");
          break;
        }
        yield();
      }

      // Read lux (may be stale if timeout triggered)
      lux = lightMeter.readLightLevel();

      // Map lux to target brightness
      byte val1 = constrain(lux, 1, 120);
      targetBrightness = map(val1, 1, 120, 40, 255);

      Serial.print("[DEBUG] Lux = ");
      Serial.print(lux);
      Serial.print(", targetBrightness = ");
      Serial.print(targetBrightness);
      Serial.print(", isDark = ");
      Serial.println(isDark);
    }

    // --- Brightness animation every 200ms ---
    if (millis() - lastBrightnessUpdate > brightnessInterval)
    {
      lastBrightnessUpdate = millis();

      byte previousBrightness = currentBrightness; // store old value
      // Update darkness flag
      isDark = lux <= 1;

      if (isDark && displayOffInDark)
      {
        currentBrightness = 0; // force off
      }
      else
      {
        // exponential moving average (EMA)
        float alpha = 0.7; // smoother 0.7-0.9
        float tempBrightness = currentBrightness;
        tempBrightness = tempBrightness * alpha + targetBrightness * (1.0 - alpha);
        currentBrightness = (byte)tempBrightness;
      }

      // Only write PWM if it changed
      if (currentBrightness != previousBrightness)
      {
        ledcWrite(LCD_LIGHT, currentBrightness);
        Serial.print("[DEBUG] Brightness = ");
        Serial.println(currentBrightness);
      }
    }

    if ((millis() - lastTime2) > timerDelay2)
    {
      if (!(isDark && displayOffInDark)) // Only read sensor if not in dark mode with offInDark enabled
      {
        Serial.println("[DEBUG] loop1: Reading temperature/humidity sensor...");
        bme.takeForcedMeasurement();
        temperature = bme.readTemperature();
        humidity = bme.readHumidity();
        pressure = bme.readPressure() / 100.0F;
        Serial.print("[DEBUG] loop1: Temp=");
        Serial.print(temperature);
        Serial.print(", Hum=");
        Serial.print(humidity);
        Serial.print(", Pressure=");
        Serial.println(pressure);
      }
      lastTime2 = millis();
    }

    static bool alarmTriggered = false; // persists across loop iterations

    if (buzzerOn && !isDark && years > 2024)
    {
      int beeps = 0;

      if (minutes == 0)
        beeps = 1;
      else if (minutes == 30)
        beeps = 2;

      if (beeps > 0)
      {
        if (!alarmTriggered) // only trigger once per minute
        {
          Serial.print("[DEBUG] Alarm triggered at ");
          Serial.print(hours);
          Serial.print(":");
          Serial.println(minutes);

          simpleBeep(beeps, 500, 255);
          alarmTriggered = true;
        }
      }
      else
      {
        // reset flag when not matching alarm minute
        alarmTriggered = false;
      }
    }
    delay(50);
  }
}

/**
 * @brief Main program loop running on Core 1
 *
 * Responsibilities:
 * - Handles GPS data processing and time synchronization
 * - Adjusts global time variables to IST immediately after GPS update
 * - Updates the display with current time, date, and sensor data
 * - Manages the menu system and user input
 * - Handles OTA updates and WiFi events if enabled
 * - Manages power-saving (dark mode) and display state
 */
void loop(void)
{ // used for blinking ":" in time (for display)
  static bool pulse = true;
  static bool wasInDarkMode = false;
  if (WiFi.status() == WL_CONNECTED)
    ElegantOTA.loop();

  byte count = 0;
  // Handle WiFi button with debounce
  if (digitalRead(wifiButtonPin) == HIGH)
  { // Button pressed (active high)
    while (digitalRead(wifiButtonPin) == HIGH)
    {
      count++;
      delay(100);
    }
    if (count > 10)
    {
      if (!pref.begin("database", false))
      { // open database
        errorMsgPrint("DATABASE", "ERROR INITIALIZE");
        delay(100); // Wait between inits
      }
      // Toggle WiFi state and save to preferences
      wifiEnabled = !wifiEnabled;
      pref.putBool("wifi_enabled", wifiEnabled);
      if (wifiEnabled)
      {
        Serial.println("WiFi Enabled - Restarting ESP32...");
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_luRS08_tr);
        u8g2.setCursor(1, 20);
        u8g2.print("CONNECTING WIFI");
        u8g2.setCursor(1, 32);
        u8g2.print("RESTARTING");
        u8g2.sendBuffer();
        pref.end();
        delay(1500); // Give time for the serial message to be sent
        ESP.restart();
      }
      else
      {
        // Stop web server
        pref.end();
        server.end();
        Serial.println("Web server stopped");
        // Disconnect WiFi
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("WiFi Disabled");
      }
    }
  }

  if (displayOffInDark && isDark)
  {
    if (!wasInDarkMode)
    {
      Serial.println("[DEBUG] loop: Entering dark mode (display off, CPU slow)");
      wasInDarkMode = true;
      setCpuFrequencyMhz(10); // Lower CPU frequency
      u8g2.clearBuffer();     // Clear display
      u8g2.setPowerSave(1);   // Turn off display
      Serial1.flush();        // Clear any pending GPS data
    }
    delay(1000);
    return;
  }
  else if (wasInDarkMode)
  {
    Serial.println("[DEBUG] loop: Exiting dark mode (display on, CPU normal)");
    wasInDarkMode = false;
    setCpuFrequencyMhz(240); // Restore CPU frequency
    u8g2.setPowerSave(0);    // Turn on display
  }

  while (gps.hdop.hdop() > 100 && gps.satellites.value() < 2)
  { // if gps signal is weak
    gpsInfo("Waiting for GPS...");
  }
  const char *ampmStr;
  while (Serial1.available())
  {
    if (gps.encode(Serial1.read()))
    { // process gps messages
      // when TinyGPSPlus reports new data...
      unsigned long age = gps.time.age();
      if (age < 500)
      {
        // Build tm struct with GPS UTC
        struct tm tmUTC = {};
        int gpsYear = gps.date.year();
        tmUTC.tm_year = gpsYear - 1900;      // struct tm wants "years since 1900"
        tmUTC.tm_mon = gps.date.month() - 1; // months 0-11
        tmUTC.tm_mday = gps.date.day();
        tmUTC.tm_hour = gps.time.hour();
        tmUTC.tm_min = gps.time.minute();
        tmUTC.tm_sec = gps.time.second();

        // Convert to epoch (UTC)
        time_t t = mktime(&tmUTC);

        // Apply IST offset (+5:30 → 19800 seconds)
        t += 19800;

        // Save epoch and also convert to broken-down local time
        currentEpoch = t;
        localtime_r(&t, &timeinfo);

        // Update globals for your display
        days = timeinfo.tm_mday;
        months = timeinfo.tm_mon + 1;
        years = timeinfo.tm_year + 1900;
        minutes = timeinfo.tm_min;
        seconds = timeinfo.tm_sec;

        // Convert to 12-hour format
        byte hour12 = timeinfo.tm_hour % 12;
        if (hour12 == 0)
          hour12 = 12; // midnight or noon → 12
        const char *ampm = (timeinfo.tm_hour < 12) ? "AM" : "PM";

        // If you want globals for 12h format:
        hours = hour12Mode ? hour12 : timeinfo.tm_hour;
        ampmStr = hour12Mode ? ampm : "";
      }
    }
  }

  if (!updateInProgress)
  { // if OTA update is not in progress

    static time_t prevEpoch = 0;
    if (currentEpoch != prevEpoch)
    {
      prevEpoch = currentEpoch;
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_t0_11_tf);
      u8g2.setCursor(2, 13);
      u8g2.print(String(temperature, 1)); // Show temperature with 1 decimal place
      u8g2.setFont(u8g2_font_threepix_tr);
      u8g2.setCursor(28, 8);
      u8g2.print("o");
      u8g2.setFont(u8g2_font_t0_11_tf);
      u8g2.setCursor(32, 13);
      u8g2.print("C ");
      String var = String(int(pressure)) + "hPa";        // Ensure font is set
      int stringWidth = u8g2.getStrWidth(var.c_str());   // Get exact pixel width
      u8g2.setCursor(((128 - stringWidth) / 2) + 3, 13); // Center using screen width
      u8g2.print(var);
      if (int(humidity) > 99)
        u8g2.setCursor(90, 13);
      else
        u8g2.setCursor(96, 13);
      u8g2.print(int(humidity));
      u8g2.print("%rH");

      u8g2.drawLine(0, 17, 127, 17);
      u8g2.setFont(u8g2_font_samim_12_t_all);
      u8g2.setCursor(4, 29);
      if (days < 10)
        u8g2.print("0");
      u8g2.print(days);
      byte x = days % 10;
      u8g2.setFont(u8g2_font_tiny_simon_tr);
      u8g2.setCursor(19, 25);

      if (days == 11 || days == 12 || days == 13)
        u8g2.print("th");
      else if (x == 1)
        u8g2.print("st");
      else if (x == 2)
        u8g2.print("nd");
      else if (x == 3)
        u8g2.print("rd");
      else
        u8g2.print("th");

      u8g2.setFont(u8g2_font_samim_12_t_all);
      u8g2.setCursor(29, 29);
      u8g2.print(monthChar[months - 1]);
      u8g2.setCursor(52, 29);
      u8g2.print(years);
      u8g2.setCursor(100, 29);
      u8g2.print(week[timeinfo.tm_wday]);

      u8g2.drawLine(0, 31, 127, 31);

      if (days == 6 && months == 9) // set this to ZERO if you don't want to show birthday message
      {                             // special message on birthday
        u8g2.setFont(u8g2_font_6x13_tr);
        u8g2.setCursor(5, 43);
        u8g2.print("HAPPY BIRTHDAY NINI!");

        u8g2.setFont(u8g2_font_logisoso16_tr);
        u8g2.setCursor(15, 63);
        if (hours < 10)
          u8g2.print("0");
        u8g2.print(hours);
        u8g2.print(pulse ? ":" : "");

        u8g2.setCursor(41, 63);
        if (minutes < 10)
          u8g2.print("0");
        u8g2.print(minutes);

        u8g2.print(pulse ? ":" : "");

        u8g2.setCursor(67, 63);
        if (seconds < 10)
          u8g2.print("0");
        u8g2.print(seconds);

        u8g2.setCursor(95, 63);
        u8g2.print(ampmStr);

        u8g2.setFont(u8g2_font_waffle_t_all);

        if (buzzerOn && !isDark)          // if buzzer on and if mute on dark is not active (or false)
          u8g2.drawUTF8(5, 54, "\ue271"); // symbol for hourly/half-hourly alarm

        if (WiFi.status() == WL_CONNECTED)
          u8g2.drawUTF8(5, 64, "\ue2b5"); // wifi-active symbol
      }
      else
      {
        // normal display
        u8g2.setFont(u8g2_font_logisoso30_tn);
        u8g2.setCursor(15, 63);
        if (hours < 10)
          u8g2.print("0");
        u8g2.print(hours);
        u8g2.print(pulse ? ":" : "");

        u8g2.setCursor(63, 63);
        if (minutes < 10)
          u8g2.print("0");
        u8g2.print(minutes);

        u8g2.setFont(u8g2_font_tenthinnerguys_tu);
        u8g2.setCursor(105, 42);
        if (seconds < 10)
          u8g2.print("0");
        u8g2.print(seconds);

        u8g2.setCursor(105, 63);
        u8g2.print(ampmStr);

        u8g2.setFont(u8g2_font_waffle_t_all);

        if (buzzerOn && !isDark)
        {
          u8g2.drawUTF8(103, 52, "\ue271");
        } // symbol for hourly/half-hourly alarm

        if (WiFi.status() == WL_CONNECTED)
          u8g2.drawUTF8(112, 52, "\ue2b5"); // wifi-active symbol
      }
      u8g2.sendBuffer();
      pulse = !pulse;
    }
  }
}

/**
 * @brief Smart delay function for GPS data processing
 * @param ms Delay duration in milliseconds
 * Ensures GPS data is processed during delay period
 */
static void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do
  {
    while (Serial1.available())
      gps.encode(Serial1.read());
  } while (millis() - start < ms);
}

/**
 * @brief Generate simple beeps using the buzzer
 * @param numBeeps Number of beeps to generate
 * @param duration_ms Duration of each beep in milliseconds
 * @param maxVolume Maximum volume level (0-255)
 */
void simpleBeep(int numBeeps, int duration_ms, byte maxVolume)
{
  int pauseTime = 150; // pause between double beeps

  if (maxVolume > 255)
    maxVolume = 255;

  for (int b = 0; b < numBeeps; b++)
  {
    // Turn on buzzer at maxVolume
    ledcWriteTone(BUZZER_PIN, PWM_FREQ);
    ledcWrite(BUZZER_PIN, maxVolume);

    // Hold for the specified duration
    delay(duration_ms);

    // Turn off buzzer
    ledcWriteTone(BUZZER_PIN, 0);
    ledcWrite(BUZZER_PIN, 0);

    // Pause between beeps
    if (b < numBeeps - 1)
      delay(pauseTime);
  }
}

/**
 * @brief Display GPS information screen
 * @param msg Status message to display
 * Shows:
 * - Number of satellites
 * - HDOP value
 * - Speed in km/h
 * - Fix age
 * - Altitude
 * - Latitude/Longitude
 */
void gpsInfo(String msg)
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_luRS08_tr);
  u8g2.setCursor(1, 9);
  u8g2.print(msg);
  u8g2.setFont(u8g2_font_5x7_mr);
  u8g2.setCursor(1, 24);
  u8g2.print("Satellites");
  u8g2.setCursor(25, 36);
  u8g2.print(gps.satellites.value());

  u8g2.setCursor(58, 24);
  u8g2.print("HDOP");
  u8g2.setCursor(58, 36);
  u8g2.print(gps.hdop.hdop());

  u8g2.setCursor(92, 24);
  u8g2.print("Speed");
  u8g2.setCursor(86, 36);
  u8g2.print(int(gps.speed.kmph()));
  u8g2.setFont(u8g2_font_micro_tr);
  u8g2.print("kmph");
  u8g2.setFont(u8g2_font_5x7_mr);

  u8g2.setCursor(1, 51);
  u8g2.print("Fix Age");
  u8g2.setCursor(6, 63);
  u8g2.print(gps.time.age());
  u8g2.print("ms"); //

  u8g2.setCursor(42, 51);
  u8g2.print("Altitude");
  u8g2.setCursor(42, 63);
  u8g2.print(gps.altitude.meters());
  u8g2.print("m");

  u8g2.setCursor(88, 51);
  u8g2.print("Lat & Lng");
  u8g2.setCursor(88, 57);
  u8g2.setFont(u8g2_font_4x6_tn);
  u8g2.print(gps.location.lat(), 7);
  u8g2.setCursor(88, 64);
  u8g2.print(gps.location.lng(), 7);

  u8g2.sendBuffer();
  smartDelay(900);
}

/**
 * @brief Display WiFi configuration instructions
 * Shows steps to:
 * 1. Enable WiFi
 * 2. Connect to AP
 * 3. Enter credentials
 */
void wifiManagerInfoPrint()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_luRS08_tr);
  u8g2.setCursor(1, 10);
  u8g2.print("Turn ON WiFi");
  u8g2.setCursor(1, 22);
  u8g2.print("on your phone/laptop.");
  u8g2.setCursor(1, 34);
  u8g2.print("Connect to->");
  u8g2.setCursor(1, 46);
  u8g2.print("SSID: WIFI_MANAGER");
  u8g2.setCursor(1, 58);
  u8g2.print("Password: WIFImanager");
  u8g2.sendBuffer();
}

/**
 * @brief Handle WiFi connection events
 * @param event WiFi event type
 * Shows configuration portal access instructions
 * when device is connected to in AP mode
 */
void WiFiEvent(WiFiEvent_t event)
{
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED)
  {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_luRS08_tr);
    u8g2.setCursor(1, 10);
    u8g2.print("On browser, go to");
    u8g2.setCursor(1, 22);
    u8g2.print("192.168.4.1/wifi");
    u8g2.setCursor(1, 34);
    u8g2.print("Enter the your Wifi");
    u8g2.setCursor(1, 46);
    u8g2.print("credentials of 2.4Ghz");
    u8g2.setCursor(1, 58);
    u8g2.print("network. Then Submit. ");
    u8g2.sendBuffer();
  }
}

/**
 * @brief Display error message with countdown
 * @param device Name of device with error
 * @param msg Error message to display
 * @note Shows message for 5 seconds with countdown
 */
void errorMsgPrint(String device, String msg)
{
  byte i = 5;
  while (i > 0)
  {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_t0_11_mf);
    u8g2.setCursor(5, 10);
    u8g2.print("ERROR: " + device);
    u8g2.drawLine(0, 11, 127, 11);
    u8g2.setCursor(5, 22);
    u8g2.print(msg);
    u8g2.setFont(u8g2_font_luRS08_tr);
    u8g2.setCursor(60, 51);
    u8g2.print(i);
    u8g2.sendBuffer();
    delay(1000);
    i--;
  }
}