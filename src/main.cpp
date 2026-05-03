#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "secrets.h"

// ===== CONFIGURATION =====

#define DHT_PIN 33 // GPIO 33 (safer than GPIO 4 which has boot-mode conflicts)
#define DHT_TYPE DHT22
#define ENABLE_DHT 1 // DHT sensor enabled with extended watchdog timeout

// ===== DISPLAY PINS =====
#define DIN_PIN 23
#define CLK_PIN 18
#define CS_PIN 5
#define NUM_DISPLAYS 4

// ===== MAX7219 REGISTERS =====
#define REG_DECODE 0x09
#define REG_INTENSITY 0x0A
#define REG_SCAN_LIMIT 0x0B
#define REG_SHUTDOWN 0x0C
#define REG_TEST 0x0F

// ===== GLOBALS =====
uint8_t framebuffer[4][8];
DHT dht(DHT_PIN, DHT_TYPE);
float currentTemp = 0;
float currentHumidity = 0;
String weatherData = "";
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 7200000; // 2 hours
bool dhtInitialized = false;

// ===== FORWARD DECLARATIONS =====
void initDisplay();
void updateDisplay();
void sendCommandToAll(uint8_t reg, uint8_t data);
void displayChar(int chip, char c);
void displayDigit(int chip, char c);
void displayWeatherIcon(int chip, String iconCode);
void displaySpanningArrow();
void animationWipeRight();
void animationWipeLeft();
void animationScroll();
void animationWaveDown();
void animationPulse();
void animationCollapseCenter();
void animationRainbow();
void displayTransition();
void connectWiFi();
void syncNTP();
void fetchWeather();
void readDHT();
void showTime();
void showWeather();
void showDHT();

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== WEATHER STATION STARTUP ===");

  // Increase watchdog timeout from 5 seconds to 10 seconds
  // DHT sensor reads can block for extended periods
  esp_task_wdt_init(10, true);

  // Initialize display
  initDisplay();

  // DO NOT initialize DHT here - will cause power spike and brownout
  // DHT will be initialized lazily on first read attempt

  // Connect WiFi
  connectWiFi();

  // Sync time with NTP (Argentina: UTC-3)
  syncNTP();

  // Fetch initial weather
  fetchWeather();

  Serial.println("Setup complete!");
}

void loop()
{
  readDHT();

  // Update weather every hour
  if (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL)
  {
    fetchWeather();
  }

  // Display rotation with transitions: Time -> Weather -> DHT -> Time -> ...
  showTime();
  displayTransition();

  showWeather();
  displayTransition();

  showDHT();
  displayTransition();
}

// ===== DISPLAY INITIALIZATION =====

void initDisplay()
{
  Serial.println("Initializing MAX7219 Display...");

  // Initialize SPI
  SPI.begin(CLK_PIN, -1, DIN_PIN, CS_PIN);
  SPI.setFrequency(1000000);
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);

  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  // Init all MAX7219 chips
  sendCommandToAll(REG_SHUTDOWN, 0x01);
  delay(10);
  sendCommandToAll(REG_SCAN_LIMIT, 0x07);
  delay(10);
  sendCommandToAll(REG_DECODE, 0x00);
  delay(10);
  sendCommandToAll(REG_INTENSITY, 0x0F);
  delay(10);

  // Clear display
  for (int row = 1; row <= 8; row++)
  {
    digitalWrite(CS_PIN, LOW);
    for (int chip = 0; chip < NUM_DISPLAYS; chip++)
    {
      SPI.write(row);
      SPI.write(0x00);
    }
    digitalWrite(CS_PIN, HIGH);
    delayMicroseconds(100);
  }

  memset(framebuffer, 0, sizeof(framebuffer));
  Serial.println("Display initialized!");
}

void updateDisplay()
{
  for (int row = 1; row <= 8; row++)
  {
    digitalWrite(CS_PIN, LOW);
    delayMicroseconds(1);

    for (int chip = 0; chip < NUM_DISPLAYS; chip++)
    {
      SPI.write(row);
      SPI.write(framebuffer[chip][row - 1]);
    }

    delayMicroseconds(1);
    digitalWrite(CS_PIN, HIGH);
    delayMicroseconds(10);
  }
  // Feed watchdog to prevent timeout during long display loops
  delay(0);
}

void sendCommandToAll(uint8_t reg, uint8_t data)
{
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(1);

  for (int i = 0; i < NUM_DISPLAYS; i++)
  {
    SPI.write(reg);
    SPI.write(data);
    delayMicroseconds(1);
  }

  delayMicroseconds(1);
  digitalWrite(CS_PIN, HIGH);
  delayMicroseconds(10);
}

// ===== CHARACTER & PATTERN DEFINITIONS =====
// All patterns are 7 pixels wide (rightmost column always empty for spacing between displays)

byte patterns[26][7] = {
    {0x18, 0x24, 0x42, 0x7E, 0x42, 0x42, 0x00}, // A
    {0x7C, 0x42, 0x7C, 0x42, 0x42, 0x7C, 0x00}, // B
    {0x3E, 0x40, 0x40, 0x40, 0x40, 0x3E, 0x00}, // C
    {0x78, 0x44, 0x42, 0x42, 0x44, 0x78, 0x00}, // D
    {0x7E, 0x40, 0x7C, 0x40, 0x40, 0x7E, 0x00}, // E
    {0x7E, 0x40, 0x7C, 0x40, 0x40, 0x40, 0x00}, // F
    {0x3E, 0x40, 0x4E, 0x42, 0x42, 0x3E, 0x00}, // G
    {0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00}, // H
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // I
    {0x0E, 0x04, 0x04, 0x04, 0x44, 0x38, 0x00}, // J
    {0x42, 0x44, 0x78, 0x50, 0x48, 0x44, 0x00}, // K
    {0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00}, // L
    {0x42, 0x66, 0x5A, 0x42, 0x42, 0x42, 0x00}, // M
    {0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x00}, // N
    {0x3C, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00}, // O
    {0x7C, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00}, // P
    {0x3C, 0x42, 0x42, 0x4A, 0x44, 0x3A, 0x00}, // Q
    {0x7C, 0x42, 0x7C, 0x48, 0x44, 0x42, 0x00}, // R
    {0x3E, 0x40, 0x3C, 0x02, 0x42, 0x3C, 0x00}, // S
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, // T
    {0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00}, // U
    {0x42, 0x42, 0x42, 0x24, 0x24, 0x18, 0x00}, // V
    {0x42, 0x42, 0x5A, 0x5A, 0x66, 0x42, 0x00}, // W
    {0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00}, // X
    {0x42, 0x24, 0x18, 0x18, 0x18, 0x18, 0x00}, // Y
    {0x7E, 0x04, 0x08, 0x10, 0x20, 0x7E, 0x00}  // Z
};

// Digit patterns (0-9) - 6 pixels wide (bits 1-6), 7 rows (rows 1-7, row 0 always clear)
byte digits[10][8] = {
    {0x00, 0x18, 0x24, 0x42, 0x42, 0x24, 0x18, 0x00}, // 0
    {0x00, 0x08, 0x18, 0x08, 0x08, 0x08, 0x1C, 0x00}, // 1
    {0x00, 0x18, 0x24, 0x04, 0x08, 0x10, 0x3C, 0x00}, // 2
    {0x00, 0x18, 0x24, 0x0C, 0x04, 0x24, 0x18, 0x00}, // 3
    {0x00, 0x04, 0x0C, 0x14, 0x24, 0x3E, 0x04, 0x00}, // 4
    {0x00, 0x3C, 0x20, 0x18, 0x04, 0x24, 0x18, 0x00}, // 5
    {0x00, 0x18, 0x20, 0x3C, 0x22, 0x22, 0x1C, 0x00}, // 6
    {0x00, 0x3C, 0x04, 0x08, 0x08, 0x10, 0x10, 0x00}, // 7
    {0x00, 0x18, 0x24, 0x18, 0x24, 0x24, 0x18, 0x00}, // 8
    {0x00, 0x18, 0x24, 0x26, 0x1A, 0x04, 0x18, 0x00}  // 9
};

// Better weather icons (8 rows each)
byte iconSun[8] = {0x00, 0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x18, 0x00};   // Sunny
byte iconCloud[8] = {0x00, 0x1C, 0x36, 0x63, 0x7E, 0x3C, 0x00, 0x00}; // Cloudy
byte iconRain[8] = {0x00, 0x1C, 0x36, 0x63, 0x55, 0xAA, 0x55, 0x00};  // Rainy
byte iconStorm[8] = {0x00, 0x1C, 0x36, 0x63, 0x99, 0x66, 0x99, 0x00}; // Storm
byte iconSnow[8] = {0x00, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55, 0x00};  // Snow
byte iconArrow[8] = {0x00, 0x08, 0x0C, 0x3E, 0x0C, 0x08, 0x00, 0x00}; // Arrow ->
byte iconMist[8] = {0x00, 0x33, 0x66, 0x33, 0x66, 0x33, 0x66, 0x00};  // Mist

void displayChar(int chip, char c)
{
  for (int row = 0; row < 8; row++)
    framebuffer[chip][row] = 0;

  if (c >= 'A' && c <= 'Z')
  {
    int idx = c - 'A';
    for (int row = 0; row < 7; row++)
    {
      // Patterns are already 7 pixels wide with spacing on right
      framebuffer[chip][row + 1] = patterns[idx][row];
    }
  }
}

void displayDigit(int chip, char digit)
{
  int val = digit - '0';
  if (val < 0 || val > 9)
    val = 0;

  for (int row = 0; row < 8; row++)
    framebuffer[chip][row] = 0;

  for (int row = 0; row < 7; row++)
  {
    // Patterns are 4 pixels wide with spacing on right, use rows 0-6, row 7 is empty
    framebuffer[chip][row] = digits[val][row];
  }
}

void displayWeatherIcon(int chip, String weatherMain)
{
  // Clear display
  for (int row = 0; row < 8; row++)
    framebuffer[chip][row] = 0;

  // Show icon based on weather type
  if (weatherMain == "Clear" || weatherMain == "Sunny")
  {
    for (int row = 0; row < 8; row++)
      framebuffer[chip][row] = iconSun[row];
  }
  else if (weatherMain == "Clouds")
  {
    for (int row = 0; row < 8; row++)
      framebuffer[chip][row] = iconCloud[row];
  }
  else if (weatherMain == "Rain" || weatherMain == "Drizzle")
  {
    for (int row = 0; row < 8; row++)
      framebuffer[chip][row] = iconRain[row];
  }
  else if (weatherMain == "Thunderstorm")
  {
    for (int row = 0; row < 8; row++)
      framebuffer[chip][row] = iconStorm[row];
  }
  else if (weatherMain == "Snow")
  {
    for (int row = 0; row < 8; row++)
      framebuffer[chip][row] = iconSnow[row];
  }
  else if (weatherMain == "Mist" || weatherMain == "Fog")
  {
    for (int row = 0; row < 8; row++)
      framebuffer[chip][row] = iconMist[row];
  }
  else
  {
    // Default: cloud
    for (int row = 0; row < 8; row++)
      framebuffer[chip][row] = iconCloud[row];
  }
}

void displayArrow(int chip)
{
  // Show arrow icon
  for (int row = 0; row < 8; row++)
    framebuffer[chip][row] = iconArrow[row];
}

void displaySpanningArrow()
{
  // Arrow shaft on chip 1 (leftmost 2 columns cleared), arrowhead on chip 2 pointing RIGHT
  byte shaft[8] = {0x00, 0x00, 0x3F, 0x3F, 0x3F, 0x3F, 0x00, 0x00};     // Line with leftmost 2 columns cleared
  byte arrowhead[8] = {0x00, 0x20, 0x30, 0x38, 0x38, 0x30, 0x20, 0x00}; // > pointing right

  for (int row = 0; row < 8; row++)
  {
    framebuffer[1][row] = shaft[row];
    framebuffer[2][row] = arrowhead[row];
  }
}

// Extract weather condition from JSON
String getWeatherMain(int forecastIndex)
{
  if (weatherData.length() == 0)
    return "Wait";

  StaticJsonDocument<2048> doc;
  deserializeJson(doc, weatherData);

  if (doc["list"].size() > forecastIndex)
  {
    JsonObject forecast = doc["list"][forecastIndex];
    if (forecast["weather"].size() > 0)
    {
      return forecast["weather"][0]["main"].as<String>();
    }
  }
  return "?";
}

int getWeatherTemp(int forecastIndex)
{
  if (weatherData.length() == 0)
    return 0;

  StaticJsonDocument<2048> doc;
  deserializeJson(doc, weatherData);

  if (doc["list"].size() > forecastIndex)
  {
    JsonObject forecast = doc["list"][forecastIndex];
    return (int)forecast["main"]["temp"];
  }
  return 0;
}

// ===== WIFI & TIME FUNCTIONS =====

void connectWiFi()
{
  Serial.println("\nConnecting to WiFi: " + String(WIFI_SSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected!");
    Serial.println("IP: " + WiFi.localIP().toString());
  }
  else
  {
    Serial.println("\nFailed to connect WiFi");
  }
}

void syncNTP()
{
  Serial.println("Syncing NTP time (Argentina UTC-3)...");
  const char *ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = -3 * 3600; // UTC-3
  const int daylightOffset_sec = 0;

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 24 * 3600 && attempts < 20)
  {
    delay(500);
    Serial.print(".");
    time(&now);
    attempts++;
  }

  Serial.println();
  Serial.println("Time synced!");
}

void fetchWeather()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected, skipping weather");
    return;
  }

  Serial.println("Fetching weather...");

  HTTPClient http;
  String url = "https://api.openweathermap.org/data/2.5/forecast?q=Villa%20Elisa,AR&units=metric&appid=" + String(OPENWEATHER_API_KEY);

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200)
  {
    weatherData = http.getString();
    lastWeatherUpdate = millis();
    Serial.println("Weather fetched OK");
  }
  else
  {
    Serial.println("Weather error: " + String(httpCode));
  }

  http.end();
}

void readDHT()
{
#if !ENABLE_DHT
  // DHT sensor disabled - will not attempt reads
  return;
#endif

  static unsigned long lastDHTRead = 0;

  // Initialize DHT on first read (avoids power spike during startup)
  if (!dhtInitialized)
  {
    Serial.println("[DHT] Initializing sensor on first read...");
    dht.begin();
    delay(500); // Give sensor time to stabilize
    dhtInitialized = true;
  }

  // Only attempt DHT read every 2 seconds
  if (millis() - lastDHTRead < 2000)
    return;

  lastDHTRead = millis();

  // Feed watchdog BEFORE DHT read (which may block)
  esp_task_wdt_reset();

  // DHT readings can block - feed watchdog during reads
  float temp = dht.readTemperature();

  // Feed watchdog between reads
  esp_task_wdt_reset();

  float humid = dht.readHumidity();

  // Feed watchdog after reads
  esp_task_wdt_reset();

  if (!isnan(temp) && !isnan(humid))
  {
    currentTemp = temp;
    currentHumidity = humid;
    Serial.printf("[DHT] Read OK - Temp: %.1f C, Humidity: %.1f %%\n", temp, humid);
  }
  else
  {
    Serial.println("[DHT] Read failed - check connections and pull-up resistor");
    currentTemp = 0;
    currentHumidity = 0;
  }
}

// ===== DISPLAY SCREENS =====

// ===== DISPLAY ANIMATIONS =====

void animationWipeRight()
{
  // Wipe from left to right across ALL 4 displays
  for (int col = 0; col < 32; col++) // 32 columns for all 4 displays
  {
    int chipStart = col / 8;
    int bitPos = 7 - (col % 8);

    for (int chip = 0; chip < NUM_DISPLAYS; chip++)
    {
      for (int row = 0; row < 8; row++)
      {
        if (chip >= chipStart)
        {
          if (chip == chipStart)
          {
            framebuffer[chip][row] &= ~(1 << bitPos);
          }
          else
          {
            framebuffer[chip][row] = 0;
          }
        }
      }
    }

    updateDisplay();
    delay(25);
  }
  Serial.println("[ANIM] Wipe Right");
}

void animationWipeLeft()
{
  // Wipe from right to left across ALL 4 displays
  for (int col = 31; col >= 0; col--)
  {
    int chipStart = col / 8;
    int bitPos = 7 - (col % 8);

    for (int chip = 0; chip < NUM_DISPLAYS; chip++)
    {
      for (int row = 0; row < 8; row++)
      {
        if (chip <= chipStart)
        {
          if (chip == chipStart)
          {
            framebuffer[chip][row] &= ~(1 << bitPos);
          }
          else
          {
            framebuffer[chip][row] = 0;
          }
        }
      }
    }

    updateDisplay();
    delay(25);
  }
  Serial.println("[ANIM] Wipe Left");
}

void animationScroll()
{
  // Scroll pattern flows left to right across all displays
  byte scrollPatterns[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

  for (int frame = 0; frame < 40; frame++)
  {
    for (int chip = 0; chip < NUM_DISPLAYS; chip++)
    {
      byte pattern = scrollPatterns[(frame + chip * 2) % 8];
      for (int row = 0; row < 8; row++)
      {
        framebuffer[chip][row] = pattern;
      }
    }
    updateDisplay();
    delay(40);
  }
  Serial.println("[ANIM] Scroll");
}

void animationWaveDown()
{
  // Wave animation flows from left to right, moving downward
  for (int wave = 0; wave < 3; wave++)
  {
    for (int offset = 0; offset < 8; offset++)
    {
      for (int chip = 0; chip < NUM_DISPLAYS; chip++)
      {
        for (int row = 0; row < 8; row++)
        {
          int waveRow = (row + offset + chip) % 8;
          framebuffer[chip][row] = (waveRow < 3) ? 0xFF : 0x00;
        }
      }
      updateDisplay();
      delay(40);
    }
  }
  Serial.println("[ANIM] Wave Down");
}

void animationPulse()
{
  // Pulse all displays brightness on/off
  for (int pulse = 0; pulse < 3; pulse++)
  {
    for (int intensity = 5; intensity <= 15; intensity++)
    {
      sendCommandToAll(REG_INTENSITY, intensity);
      delay(20);
    }

    for (int intensity = 15; intensity >= 0; intensity--)
    {
      sendCommandToAll(REG_INTENSITY, intensity);
      delay(20);
    }
  }

  sendCommandToAll(REG_INTENSITY, 0x0F);
  Serial.println("[ANIM] Pulse");
}

void animationCollapseCenter()
{
  // Displays collapse from edges to center
  for (int collapse = 0; collapse < 8; collapse++)
  {
    byte mask = 0xFF >> collapse;

    // Left displays collapse from left edge
    for (int chip = 0; chip < 2; chip++)
    {
      for (int row = 0; row < 8; row++)
      {
        framebuffer[chip][row] &= mask;
      }
    }

    // Right displays collapse from right edge
    for (int chip = 2; chip < 4; chip++)
    {
      for (int row = 0; row < 8; row++)
      {
        framebuffer[chip][row] &= (mask << collapse) | (0xFF >> (8 - collapse));
      }
    }

    updateDisplay();
    delay(50);
  }
  Serial.println("[ANIM] Collapse");
}

void animationRainbow()
{
  // Progressive pixel lighting from left to right
  for (int pos = 0; pos < 32; pos++)
  {
    int chip = pos / 8;
    int bitPos = 7 - (pos % 8);

    for (int row = 0; row < 8; row++)
    {
      if (pos > 0)
      {
        int prevChip = (pos - 1) / 8;
        int prevBit = 7 - ((pos - 1) % 8);
        if (prevChip == chip)
        {
          framebuffer[chip][row] |= (1 << prevBit);
        }
      }
      framebuffer[chip][row] |= (1 << bitPos);
    }

    updateDisplay();
    delay(30);
  }

  // Then clear it back
  for (int pos = 31; pos >= 0; pos--)
  {
    int chip = pos / 8;
    int bitPos = 7 - (pos % 8);

    for (int row = 0; row < 8; row++)
    {
      framebuffer[chip][row] &= ~(1 << bitPos);
    }

    updateDisplay();
    delay(30);
  }
  Serial.println("[ANIM] Rainbow");
}

void displayTransition()
{
  // Pick a random animation (0-5) - all flow across all 4 displays
  int animation = random(0, 6);

  switch (animation)
  {
  case 0:
    animationWipeRight();
    break;
  case 1:
    animationWipeLeft();
    break;
  case 2:
    animationScroll();
    break;
  case 3:
    animationWaveDown();
    break;
  case 4:
    animationPulse();
    break;
  case 5:
    animationCollapseCenter();
    break;
  }

  // Clear all displays and reinitialize
  for (int chip = 0; chip < NUM_DISPLAYS; chip++)
  {
    for (int row = 0; row < 8; row++)
    {
      framebuffer[chip][row] = 0;
    }
  }

  for (int i = 0; i < 3; i++)
  {
    updateDisplay();
    delay(100);
  }

  // Reinitialize display to fix any glitches
  initDisplay();
}

void showTime()
{
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);

  int hour = timeinfo->tm_hour;
  int minute = timeinfo->tm_min;

  char h1 = '0' + (hour / 10);
  char h2 = '0' + (hour % 10);
  char m1 = '0' + (minute / 10);
  char m2 = '0' + (minute % 10);

  displayDigit(0, h1);
  displayDigit(1, h2);
  displayDigit(2, m1);
  displayDigit(3, m2);

  Serial.printf("[DISPLAY] TIME: %02d:%02d\n", hour, minute);

  // Show time for ~2 seconds instead of 3 to avoid watchdog timeout
  for (int i = 0; i < 10; i++)
  {
    updateDisplay();
    delay(180);
  }
}

void showWeather()
{
  if (weatherData.length() == 0)
  {
    // No weather data yet
    displayChar(0, 'W');
    displayChar(1, 'A');
    displayChar(2, 'I');
    displayChar(3, 'T');
    Serial.println("[DISPLAY] WEATHER: Waiting (WAIT)");
  }
  else
  {
    // Get current (index 0) and next hour (index 1) weather
    String currentMain = getWeatherMain(0);
    String nextMain = getWeatherMain(1);

    // Display layout: [Current Icon] [Arrow] [Next Hour Icon]
    // Chip 0 (far left): Current weather icon
    displayWeatherIcon(0, currentMain);

    // Chips 1-2 (middle): Static arrow spanning both displays
    displaySpanningArrow();

    // Chip 3 (far right): Next hour weather icon
    displayWeatherIcon(3, nextMain);

    Serial.printf("[DISPLAY] WEATHER: %s -> %s\n",
                  currentMain.c_str(), nextMain.c_str());
  }

  // Show weather for ~2 seconds
  for (int i = 0; i < 10; i++)
  {
    updateDisplay();
    delay(180);
  }
}

void showDHT()
{
  int temp = (int)currentTemp;
  int humid = (int)currentHumidity;

  displayChar(0, 'T');
  displayDigit(1, '0' + (temp / 10));
  displayDigit(2, '0' + (temp % 10));
  displayChar(3, 'C');

  Serial.printf("[DISPLAY] TEMP: %d C\n", temp);

  // Show temp for ~2 seconds
  for (int i = 0; i < 10; i++)
  {
    updateDisplay();
    delay(180);
  }

  displayChar(0, 'H');
  displayDigit(1, '0' + (humid / 10));
  displayDigit(2, '0' + (humid % 10));
  displayChar(3, '%');

  Serial.printf("[DISPLAY] HUMIDITY: %d %%\n", humid);

  // Show humidity for ~2 seconds
  for (int i = 0; i < 10; i++)
  {
    updateDisplay();
    delay(180);
  }
}
