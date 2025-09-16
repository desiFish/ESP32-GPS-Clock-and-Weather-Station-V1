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
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <BH1750.h>

#include <ArduinoJson.h>

#include <TimeLib.h>
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
#define SWVersion "v1.2.0"

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
          //digitalWrite(buzzerPin, buzzerOn ? HIGH : LOW);
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
float temp2, hum;
BH1750 lightMeter; // object light sensor

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, 18, 23, 5, U8X8_PIN_NONE);

#define lcdBrightnessPin 4 // PWM pin for LCD backlight control
#define lcdEnablePin 33    // LCD enable pin
#define buzzerPin 25       // Buzzer control pin

char week[7][12] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char monthChar[12][12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// variables for holding time data globally
byte days = 0, months = 0, hours = 0, minutes = 0, seconds = 0;
int years = 0;

bool isDark; // Tracks ambient light state

// LUX (BH1750) update frequency
unsigned long lastTime1 = 0;   // Last light sensor update
const long timerDelay1 = 2000; // Light sensor update interval (2 seconds)

// BME280 update frequency
unsigned long lastTime2 = 0;    // Last temperature sensor update
const long timerDelay2 = 60000; // Temperature update interval (1 minute)

byte pulse = 0; // Used for blinking time separator

time_t prevDisplay = 0; // when the digital clock was displayed

// for creating task attached to CORE 0 of CPU
TaskHandle_t loop1Task;

// Add this with other global variables
byte currentBrightness = 250; // Track current brightness level

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
  delay(100); // Give I2C bus time to stabilize

  // Initialize WiFi button pin
  pinMode(wifiButtonPin, INPUT);

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
  delay(100);

  loadConfig(); // <-- Load config from LittleFS before anything else

  // Continue with normal setup if not going to deep sleep
  if (getCpuFrequencyMhz() != 240)
    setCpuFrequencyMhz(240); // if not 160MHz, set to 160MHz

  pinMode(gpsPin, OUTPUT);
  digitalWrite(gpsPin, HIGH); // you can connect the gps directly to 3.3V pin
  pinMode(lcdBrightnessPin, OUTPUT);
  analogWrite(lcdBrightnessPin, 50);
  pinMode(lcdEnablePin, OUTPUT);
  digitalWrite(lcdEnablePin, HIGH);
  pinMode(buzzerPin, OUTPUT);

  Serial1.begin(9600, SERIAL_8N1, RXPin, TXPin);

  analogWrite(lcdBrightnessPin, 250);
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.drawLine(0, 17, 127, 17);
  u8g2.setFont(u8g2_font_7x14B_mr);
  u8g2.setCursor(12, 30);
  u8g2.print("GPS Clock V1");
  u8g2.drawLine(0, 31, 127, 31);
  u8g2.sendBuffer();

  // Initialize I2C devices with proper delays
  delay(100);   // Allow I2C bus to settle
  Wire.begin(); // Reinitialize I2C bus
  delay(100);   // Wait after reinit

  if (!lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE))
  {
    errorMsgPrint("BH1750", "CANNOT FIND");
    delay(100); // Wait between sensor inits
  }

  if (!pref.begin("database", false))
  { // open database
    errorMsgPrint("DATABASE", "ERROR INITIALIZE");
    delay(100); // Wait between inits
  }

  // Restore WiFi state from preferences (default to true if not set)
  wifiEnabled = pref.getBool("wifi_enabled", true);

  // Try BME280 initialization multiple times if needed
  bool bmeFound = false;
  for (int i = 0; i < 3; i++)
  { // Try up to 3 times
    if (bme.begin(0x76))
    {
      bmeFound = true;
      break;
    }
    delay(100); // Wait before retrying
  }

  if (!bmeFound)
  {
    errorMsgPrint("BME280", "CANNOT FIND");
  }

  Serial.println("BME Ready");

  // Set up oversampling and filter initialization
  bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                  Adafruit_BME280::SAMPLING_X16,     // temperature
                  Adafruit_BME280::SAMPLING_NONE,    // pressure
                  Adafruit_BME280::SAMPLING_X16,     // humidity
                  Adafruit_BME280::FILTER_X16,       // filter
                  Adafruit_BME280::STANDBY_MS_1000); // set delay between measurements
  delay(500);

  // wifi manager
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
        ;
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

  xTaskCreatePinnedToCore(
      loop1,       // Task function.
      "loop1Task", // name of task.
      10000,       // Stack size of task
      NULL,        // parameter of the task
      1,           // priority of the task
      &loop1Task,  // Task handle to keep track of created task
      0);          // pin task to core 0

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
  temp2 = bme.readTemperature();
  hum = bme.readHumidity();
}

// RUNS ON CORE 0
void loop1(void *pvParameters)
{
  for (;;)
  {
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

    // Original light sensor code
    if ((millis() - lastTime1) > timerDelay1)
    {
      lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE);
      float lux;
      while (!lightMeter.measurementReady(true))
      {
        yield();
      }
      lux = lightMeter.readLightLevel();

      Serial.println("LUXRaw: ");
      Serial.println(lux);

      isDark = true && (lux == 0); // Check if it's dark only if muteDark is enabled

      // Improved brightness control with smooth transitions
      if (true)
      {
        byte targetBrightness;
        if (lux == 0)
        {
          if (displayOffInDark)
            targetBrightness = 0; // Turn off display in dark if enabled
          else
            targetBrightness = 5; // Minimum brightness when very dark
        }
        else
        {
          byte val1 = constrain(lux, 1, 120);
          targetBrightness = map(val1, 1, 120, 40, 255);
        }

        // Improved smooth transition with dynamic step size
        if (currentBrightness != targetBrightness)
        {
          int diff = targetBrightness - currentBrightness;
          // Calculate step size based on difference
          // Larger differences = larger steps, smaller differences = smaller steps
          byte stepSize = max(1, min(abs(diff) / 4, 15));

          if (diff > 0)
          {
            currentBrightness = min(255, currentBrightness + stepSize);
          }
          else
          {
            currentBrightness = max(5, currentBrightness - stepSize);
          }
        }

        Serial.println("Brightness: ");
        Serial.println(currentBrightness);
        analogWrite(lcdBrightnessPin, currentBrightness);
      }

      lastTime1 = millis();
    }
    if ((millis() - lastTime2) > timerDelay2)
    {
      temp2 = bme.readTemperature();
      // press = bme.readPressure() / 100.0F;
      hum = bme.readHumidity();

      lastTime2 = millis();
    }

    if (buzzerOn && !isDark && (seconds == 0)) // chime
    {
      switch (minutes)
      {
      case 0:
        if (true)
          buzzer(600, 1);
        break;
      case 30:
        if (true)
          buzzer(400, 2);
        break;
      }
    }
    delay(100);
  }
}

// RUNS ON CORE 1
void loop(void)
{                                      // Main loop for display and GPS operations
  static bool lastDisplayState = true; // Track last display state
  if (true)
    ElegantOTA.loop();
  // No need to call server.loop() for ESPAsyncWebServer
  bool displayOff = false; // temporary, change to actual variable
  if (displayOffInDark)
  {
    displayOff = isDark;
  }

  // Only execute display state changes when the state actually changes
  if (!displayOff != lastDisplayState)
  {                                 // State has changed
    lastDisplayState = !displayOff; // Update state tracker

    if (!displayOff)
    {
      // Turn display ON only when transitioning from OFF to ON
      setCpuFrequencyMhz(240); // set to 160MHz when display is ON
      digitalWrite(lcdEnablePin, HIGH);
      delay(500); // wait for display to stabilize
    }
    else
    {
      // Turn display OFF only when transitioning from ON to OFF
      u8g2.clearBuffer(); // clear display
      u8g2.sendBuffer();
      delay(500);                      // wait for display to stabilize
      setCpuFrequencyMhz(80);          // reduce CPU speed to save power
      digitalWrite(lcdEnablePin, LOW); // turn off display
    }
  }

  if (!displayOff)
  {
    while (gps.hdop.hdop() > 100 && gps.satellites.value() < 2)
    { // if gps signal is weak
      gpsInfo("Waiting for GPS...");
    }

    while (Serial1.available())
    {
      if (gps.encode(Serial1.read()))
      { // process gps messages
        // when TinyGPSPlus reports new data...
        unsigned long age = gps.time.age();
        if (age < 500)
        {
          // set the Time according to the latest GPS reading
          setTime(gps.time.hour(), gps.time.minute(), gps.time.second(), gps.date.day(), gps.date.month(), gps.date.year());
          adjustTime(19800);
        }
      }
    }

    days = day();
    months = month();
    years = year();
    hours = hourFormat12();
    minutes = minute();
    seconds = second();

    if (!updateInProgress)
    { // if OTA update is not in progress
      if (timeStatus() != timeNotSet)
      { // if time is set
        if (now() != prevDisplay)
        { // update the display only if the time has changed
          prevDisplay = now();
          u8g2.clearBuffer();
          u8g2.setFont(u8g2_font_pixzillav1_tr);
          u8g2.setCursor(5, 15);
          u8g2.print(temp2, 2);
          u8g2.setFont(u8g2_font_threepix_tr);
          u8g2.setCursor(40, 8);
          u8g2.print("o");
          u8g2.setFont(u8g2_font_pixzillav1_tr);
          u8g2.setCursor(45, 15);
          u8g2.print("C ");
          u8g2.setCursor(64, 15);
          u8g2.print(hum, 2);
          u8g2.setCursor(100, 15);
          u8g2.print("%rH");

          u8g2.drawLine(0, 17, 127, 17);
          u8g2.setFont(u8g2_font_7x14B_mr);
          u8g2.setCursor(8, 30);
          if (days < 10)
            u8g2.print("0");
          u8g2.print(days);
          byte x = days % 10;
          u8g2.setFont(u8g2_font_profont10_mr);
          u8g2.setCursor(22, 25);

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

          u8g2.setFont(u8g2_font_7x14B_mr);
          u8g2.setCursor(34, 30);
          u8g2.print(monthChar[months - 1]);
          u8g2.setCursor(58, 30);
          u8g2.print(years);
          u8g2.setCursor(102, 30);
          u8g2.print(week[weekday() - 1]);

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
            if (pulse == 0)
              u8g2.print(":");
            else
              u8g2.print("");

            u8g2.setCursor(41, 63);
            if (minutes < 10)
              u8g2.print("0");
            u8g2.print(minutes);

            if (pulse == 0)
              u8g2.print(":");
            else
              u8g2.print("");

            u8g2.setCursor(67, 63);
            if (seconds < 10)
              u8g2.print("0");
            u8g2.print(seconds);

            u8g2.setCursor(95, 63);
            u8g2.print(isAM() ? "AM" : "PM");

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
            if (pulse == 0)
              u8g2.print(":");
            else
              u8g2.print("");

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
            u8g2.print(isAM() ? "AM" : "PM");

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
 * @brief Generate beep pattern
 * @param Delay Duration of each beep in milliseconds
 * @param count Number of beeps to generate
 * Used for hourly and half-hourly chimes
 */
void buzzer(int Delay, byte count)
{
  for (int i = 0; i < count; i++)
  {
    digitalWrite(buzzerPin, HIGH);
    delay(Delay);
    digitalWrite(buzzerPin, LOW);
    delay(Delay);
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