#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <WiFiManager.h>

#include <YoutubeApi.h>
#include <ArduinoJson.h>

#include <Adafruit_NeoPixel.h>

#if defined(__has_include)
  #if __has_include("secrets.h")
    #include "secrets.h"
  #endif
#endif

#ifndef YT_WIFI_SSID
  #define YT_WIFI_SSID ""
#endif

#ifndef YT_WIFI_PASSWORD
  #define YT_WIFI_PASSWORD ""
#endif

#ifndef YT_API_KEY
  #define YT_API_KEY ""
#endif

#ifndef YT_CHANNEL_ID
  #define YT_CHANNEL_ID ""
#endif

#ifndef YT_DISPLAY_DEMO
  #define YT_DISPLAY_DEMO 0
#endif

#ifndef YT_DISPLAY_DEMO_DELTA
  #define YT_DISPLAY_DEMO_DELTA 9
#endif

#ifndef YT_DISPLAY_DEMO_COUNT
  #define YT_DISPLAY_DEMO_COUNT 18709
#endif

// LED MATRIX DISPLAY DEFINITION
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES  4
#define CLK_PIN   D6  // or SCK
#define DATA_PIN  D8  // or MOSI
#define CS_PIN    D7  // or SS

#define PIN   D4
#define LED_NUM 3 // CHANGE this to the number of LEDs you have

const char* CONFIG_FILE = "/config.json";
const char* CONFIG_PORTAL_SSID = "YouTubePlayButtonSetup";
const unsigned long SUBSCRIBER_FETCH_INTERVAL_MS = 3UL * 60UL * 1000UL;
const unsigned long DELTA_SEQUENCE_DISPLAY_MS = 10000UL;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
const uint8_t DISPLAY_COLUMN_COUNT = MAX_DEVICES * 8;
const uint8_t WAVE_FRAME_COUNT = 14;
const uint8_t WAVE_FRAME_DELAY_MS = 60;
const uint8_t DELTA_WAVE_MAX_WIDTH = 8;
const uint8_t DELTA_PLUS_WIDTH = 3;
const uint8_t DELTA_PLUS_SPACING = 1;
const uint8_t CHAR_SPACING = 1;
const unsigned long ACCENT_LED_STEP_MS = 120;

const char* ERROR_TEXT_API_KEY = "No API";
const char* ERROR_TEXT_CHANNEL_ID = "CH ID?";
const char* ERROR_TEXT_WIFI = "WiFi?";
const char* ERROR_TEXT_API_FETCH = "API?";

char youtubeApiKey[80] = "";
char youtubeChannelId[48] = "";
bool shouldSaveConfig = false;
unsigned long lastSubscriberFetch = 0;
long lastKnownSubscriberCount = -1;
bool hasLastKnownSubscriberCount = false;

enum FetchError {
  FETCH_ERROR_NONE,
  FETCH_ERROR_WIFI,
  FETCH_ERROR_API
};

FetchError lastFetchError = FETCH_ERROR_NONE;

Adafruit_NeoPixel leds = Adafruit_NeoPixel(LED_NUM, PIN, NEO_GRB + NEO_KHZ800);
MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

WiFiClientSecure client;
YoutubeApi api(youtubeApiKey, client);

unsigned long lastAccentLedStep = 0;
uint8_t accentLedStep = 0;

void saveConfigCallback() {
  shouldSaveConfig = true;
}

bool hasText(const char* value) {
  return value != nullptr && strlen(value) > 0;
}

bool applyEmbeddedSecrets() {
  bool changed = false;

  if (hasText(YT_API_KEY) && strncmp(YT_API_KEY, youtubeApiKey, sizeof(youtubeApiKey)) != 0) {
    strlcpy(youtubeApiKey, YT_API_KEY, sizeof(youtubeApiKey));
    changed = true;
  }

  if (hasText(YT_CHANNEL_ID) && strncmp(YT_CHANNEL_ID, youtubeChannelId, sizeof(youtubeChannelId)) != 0) {
    strlcpy(youtubeChannelId, YT_CHANNEL_ID, sizeof(youtubeChannelId));
    changed = true;
  }

  return changed;
}

bool initializeFileSystem() {
  if (LittleFS.begin()) {
    return true;
  }

  Serial.println(F("LittleFS mount failed. Formatting filesystem."));
  if (!LittleFS.format()) {
    Serial.println(F("LittleFS format failed."));
    return false;
  }

  return LittleFS.begin();
}

bool loadConfig() {
  if (!initializeFileSystem()) {
    return false;
  }

  if (!LittleFS.exists(CONFIG_FILE)) {
    return false;
  }

  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println(F("Failed to open config file."));
    return false;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.print(F("Failed to parse config file: "));
    Serial.println(error.c_str());
    return false;
  }

  strlcpy(youtubeApiKey, doc["apiKey"] | "", sizeof(youtubeApiKey));
  strlcpy(youtubeChannelId, doc["channelId"] | "", sizeof(youtubeChannelId));
  if (doc.containsKey("lastSubscriberCount")) {
    lastKnownSubscriberCount = doc["lastSubscriberCount"].as<long>();
    hasLastKnownSubscriberCount = lastKnownSubscriberCount >= 0;
  }

  return strlen(youtubeApiKey) > 0 && strlen(youtubeChannelId) > 0;
}

bool saveConfig() {
  if (!initializeFileSystem()) {
    return false;
  }

  StaticJsonDocument<256> doc;
  doc["apiKey"] = youtubeApiKey;
  doc["channelId"] = youtubeChannelId;
  if (hasLastKnownSubscriberCount) {
    doc["lastSubscriberCount"] = lastKnownSubscriberCount;
  }

  File configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println(F("Failed to open config file for writing."));
    return false;
  }

  size_t bytesWritten = serializeJson(doc, configFile);
  configFile.close();

  if (bytesWritten == 0) {
    Serial.println(F("Failed to write config file."));
    return false;
  }

  Serial.println(F("Config saved."));
  return true;
}

bool copyConfigValue(const char* value, char* target, size_t targetSize) {
  if (!hasText(value)) {
    return false;
  }

  if (strncmp(value, target, targetSize) == 0) {
    return false;
  }

  strlcpy(target, value, targetSize);
  return true;
}

bool connectWithEmbeddedWifi() {
  if (!hasText(YT_WIFI_SSID)) {
    return false;
  }

  Serial.print(F("Connecting to embedded Wi-Fi SSID: "));
  Serial.println(F(YT_WIFI_SSID));
  displayCenteredText("WiFi");

  WiFi.begin(YT_WIFI_SSID, YT_WIFI_PASSWORD);
  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    waitWithAccentLeds(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Connected. IP: "));
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println(F("Embedded Wi-Fi connect timed out. Falling back to setup portal."));
  WiFi.disconnect();
  return false;
}

void connectWifiAndLoadConfig() {
  loadConfig();
  bool embeddedConfigChanged = applyEmbeddedSecrets();
  bool hasConfig = hasText(youtubeApiKey) && hasText(youtubeChannelId);

  WiFi.mode(WIFI_STA);

  WiFiManager wifiManager;
  wifiManager.setSaveParamsCallback(saveConfigCallback);

  WiFiManagerParameter apiKeyParameter("api_key", "YouTube API key", youtubeApiKey, sizeof(youtubeApiKey));
  WiFiManagerParameter channelIdParameter("channel_id", "YouTube Channel ID", youtubeChannelId, sizeof(youtubeChannelId));
  wifiManager.addParameter(&apiKeyParameter);
  wifiManager.addParameter(&channelIdParameter);

  bool connected = hasConfig && connectWithEmbeddedWifi();

  if (!connected) {
    displayCenteredText(hasConfig ? "WiFi" : "Setup");

    connected = hasConfig
      ? wifiManager.autoConnect(CONFIG_PORTAL_SSID)
      : wifiManager.startConfigPortal(CONFIG_PORTAL_SSID);
  }

  if (!connected) {
    Serial.println(F("Failed to connect or configure Wi-Fi."));
    displayCenteredText(ERROR_TEXT_WIFI);
    waitWithAccentLeds(3000);
    ESP.restart();
  }

  bool configChanged = false;
  configChanged |= copyConfigValue(apiKeyParameter.getValue(), youtubeApiKey, sizeof(youtubeApiKey));
  configChanged |= copyConfigValue(channelIdParameter.getValue(), youtubeChannelId, sizeof(youtubeChannelId));

  if (shouldSaveConfig || configChanged || embeddedConfigChanged) {
    saveConfig();
  }

  if (strlen(youtubeApiKey) == 0) {
    Serial.println(F("Missing YouTube API key."));
    displayCenteredText(ERROR_TEXT_API_KEY);
    waitWithAccentLeds(3000);
    ESP.restart();
  }

  if (strlen(youtubeChannelId) == 0) {
    Serial.println(F("Missing YouTube channel ID."));
    displayCenteredText(ERROR_TEXT_CHANNEL_ID);
    waitWithAccentLeds(3000);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);

  leds.begin();

  // Initialize the LED Matrix 4, 8x8.
  myDisplay.begin();
  myDisplay.setIntensity(10);
  myDisplay.setTextAlignment(PA_CENTER);
  myDisplay.setPause(2000);
  myDisplay.setSpeed(40);
  displayCenteredText("Hello");

  waitWithAccentLeds(1000);

#if YT_DISPLAY_DEMO
  return;
#endif

  connectWifiAndLoadConfig();

  displayCenteredText("Done!");

  client.setInsecure();
  handleFetchSubscribers();
}

void updateAccentLeds() {
  static const uint8_t colors[][3] = {
    {50, 50, 50},
    {80, 80, 120},
    {120, 80, 80},
    {80, 120, 80},
    {120, 110, 70},
    {70, 120, 120}
  };
  const uint8_t colorCount = sizeof(colors) / sizeof(colors[0]);

  if (millis() - lastAccentLedStep < ACCENT_LED_STEP_MS) {
    return;
  }

  lastAccentLedStep = millis();

  for (uint8_t i = 0; i < LED_NUM; i++) {
    uint8_t colorIndex = (accentLedStep + i) % colorCount;
    leds.setPixelColor(i, leds.Color(colors[colorIndex][0], colors[colorIndex][1], colors[colorIndex][2]));
  }

  leds.show();
  accentLedStep = (accentLedStep + 1) % colorCount;
}

void waitWithAccentLeds(unsigned long durationMs) {
  unsigned long startedAt = millis();

  while (millis() - startedAt < durationMs) {
    updateAccentLeds();
    delay(20);
    yield();
  }
}

String formatTwoDigits(unsigned int value) {
  if (value < 10) {
    return "0" + String(value);
  }

  return String(value);
}

String formatSubscriberCount(long count) {
  unsigned long displayCount = count > 0 ? count : 0;

  if (displayCount < 1000UL) {
    return String(displayCount);
  } else if (displayCount < 10000UL) {
    return String(displayCount / 1000UL) + "." + formatTwoDigits((displayCount % 1000UL) / 10UL) + "K";
  } else if (displayCount < 100000UL) {
    return String(displayCount / 1000UL) + "." + String((displayCount % 1000UL) / 100UL) + "K";
  } else if (displayCount < 1000000UL) {
    return String(displayCount / 1000UL) + "K";
  } else if (displayCount < 10000000UL) {
    return String(displayCount / 1000000UL) + "." + formatTwoDigits((displayCount % 1000000UL) / 10000UL) + "M";
  } else if (displayCount < 100000000UL) {
    return String(displayCount / 1000000UL) + "." + String((displayCount % 1000000UL) / 100000UL) + "M";
  } else if (displayCount < 1000000000UL) {
    return String(displayCount / 1000000UL) + "M";
  }

  return String(displayCount / 1000000000UL) + "B";
}

String formatPublicDisplayStepMagnitude(unsigned long delta, unsigned long currentCount) {
  if (currentCount < 1000UL) {
    return String(delta);
  } else if (currentCount < 10000UL && delta >= 10UL) {
    return String(delta / 1000UL) + "." + formatTwoDigits((delta % 1000UL) / 10UL) + "K";
  } else if (currentCount < 100000UL && delta >= 100UL) {
    return String(delta / 1000UL) + "." + String((delta % 1000UL) / 100UL) + "K";
  } else if (currentCount < 1000000UL && delta >= 1000UL) {
    return String(delta / 1000UL) + "K";
  } else if (currentCount < 10000000UL && delta >= 10000UL) {
    return String(delta / 1000000UL) + "." + formatTwoDigits((delta % 1000000UL) / 10000UL) + "M";
  } else if (currentCount < 100000000UL && delta >= 100000UL) {
    return String(delta / 1000000UL) + "." + String((delta % 1000000UL) / 100000UL) + "M";
  } else if (currentCount < 1000000000UL && delta >= 1000000UL) {
    return String(delta / 1000000UL) + "M";
  } else if (currentCount >= 1000000000UL && delta >= 1000000000UL) {
    return String(delta / 1000000000UL) + "B";
  }

  return formatCompactDeltaMagnitude(delta);
}

String formatPublicDisplayStepDelta(long delta, long currentCount) {
  long absoluteDelta = labs(delta);
  unsigned long displayContext = currentCount > 0 ? currentCount : absoluteDelta;

  if (delta < 0) {
    return "-" + formatPublicDisplayStepMagnitude(absoluteDelta, displayContext);
  }

  if (delta > 0) {
    return "+" + formatPublicDisplayStepMagnitude(absoluteDelta, displayContext);
  }

  return "0";
}

String formatCompactDeltaMagnitude(unsigned long absoluteDelta) {
  if (absoluteDelta < 1000) {
    return String(absoluteDelta);
  }

  if (absoluteDelta < 1000000UL) {
    return String(absoluteDelta / 1000UL) + "K";
  }

  if (absoluteDelta < 1000000000UL) {
    return String(absoluteDelta / 1000000UL) + "M";
  }

  return String(absoluteDelta / 1000000000UL) + "B";
}

uint8_t measureTextWidth(const String& text) {
  MD_MAX72XX* matrix = myDisplay.getGraphicObject();
  uint8_t cBuf[8];
  uint8_t width = 0;

  for (uint8_t i = 0; i < text.length(); i++) {
    width += matrix->getChar(text.charAt(i), sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
    if (i < text.length() - 1) {
      width += CHAR_SPACING;
    }
  }

  return width;
}

uint8_t bottomAlignFontColumn(uint8_t columnData) {
  return columnData << 1;
}

void drawCenteredText(MD_MAX72XX* matrix, const String& text, uint8_t textWidth) {
  int16_t col = ((DISPLAY_COLUMN_COUNT + textWidth) / 2) - 1;
  drawTextAt(matrix, text, col - textWidth + 1, textWidth);
}

void drawTextAt(MD_MAX72XX* matrix, const String& text, int16_t startColumn, uint8_t textWidth) {
  uint8_t cBuf[8];
  int16_t col = startColumn + textWidth - 1;

  for (uint8_t i = 0; i < text.length(); i++) {
    uint8_t charWidth = matrix->getChar(text.charAt(i), sizeof(cBuf) / sizeof(cBuf[0]), cBuf);

    for (uint8_t c = 0; c < charWidth; c++) {
      if (col >= 0 && col < DISPLAY_COLUMN_COUNT) {
        matrix->setColumn(col, bottomAlignFontColumn(cBuf[c]));
      }
      col--;
    }

    if (i < text.length() - 1) {
      for (uint8_t space = 0; space < CHAR_SPACING; space++) {
        if (col >= 0 && col < DISPLAY_COLUMN_COUNT) {
          matrix->setColumn(col, 0);
        }
        col--;
      }
    }
  }
}

void drawSmallPlus(MD_MAX72XX* matrix, uint8_t startColumn) {
  matrix->setPoint(4, startColumn, true);
  matrix->setPoint(3, startColumn + 1, true);
  matrix->setPoint(4, startColumn + 1, true);
  matrix->setPoint(5, startColumn + 1, true);
  matrix->setPoint(4, startColumn + 2, true);
}

void displayCenteredText(const String& text) {
  MD_MAX72XX* matrix = myDisplay.getGraphicObject();
  uint8_t textWidth = measureTextWidth(text);

  matrix->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  matrix->clear();
  drawCenteredText(matrix, text, textWidth);
  matrix->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void drawSideWave(MD_MAX72XX* matrix, uint8_t startColumn, uint8_t waveWidth, uint8_t phase, bool mirror) {
  static const uint8_t waveRows[] = {1, 0, 0, 1, 2, 2, 1, 0};
  const uint8_t patternWidth = sizeof(waveRows) / sizeof(waveRows[0]);

  for (uint8_t i = 0; i < waveWidth; i++) {
    uint8_t col = startColumn + i;
    uint8_t patternIndex = mirror
      ? ((waveWidth - 1 - i + phase) % patternWidth)
      : ((i + phase) % patternWidth);
    uint8_t topRow = waveRows[patternIndex];

    matrix->setPoint(topRow, col, true);
    matrix->setPoint(7 - topRow, col, true);
  }
}

void drawDeltaViewFrame(const String& deltaValue, uint8_t phase) {
  MD_MAX72XX* matrix = myDisplay.getGraphicObject();
  uint8_t textWidth = measureTextWidth(deltaValue);
  uint8_t contentWidth = DELTA_PLUS_WIDTH + DELTA_PLUS_SPACING + textWidth;
  int16_t contentStart = max(0, (DISPLAY_COLUMN_COUNT - contentWidth) / 2);
  uint8_t availableSideWidth = 0;

  if (contentWidth < DISPLAY_COLUMN_COUNT) {
    availableSideWidth = (DISPLAY_COLUMN_COUNT - contentWidth) / 2;
  }

  uint8_t requestedWaveWidth = availableSideWidth > 1 ? availableSideWidth - 1 : 0;
  uint8_t waveWidth = min(DELTA_WAVE_MAX_WIDTH, requestedWaveWidth);

  matrix->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  matrix->clear();

  if (waveWidth > 0) {
    drawSideWave(matrix, 0, waveWidth, phase, false);
    drawSideWave(matrix, DISPLAY_COLUMN_COUNT - waveWidth, waveWidth, phase, true);
  }

  drawTextAt(matrix, deltaValue, contentStart, textWidth);
  drawSmallPlus(matrix, contentStart + textWidth + DELTA_PLUS_SPACING);

  matrix->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void animateSubscriberDelta(long delta, long currentCount) {
  if (delta <= 0) {
    return;
  }

  String deltaValue = formatPublicDisplayStepMagnitude(delta, currentCount > 0 ? currentCount : delta);
  unsigned long startedAt = millis();
  uint8_t frame = 0;

  do {
    drawDeltaViewFrame(deltaValue, frame++);
    waitWithAccentLeds(WAVE_FRAME_DELAY_MS);
  } while (millis() - startedAt < DELTA_SEQUENCE_DISPLAY_MS);
}

void displayDemoText(const String& text, unsigned long durationMs) {
  displayCenteredText(text);
  waitWithAccentLeds(durationMs);
}

void runDisplayDemo() {
  animateSubscriberDelta(YT_DISPLAY_DEMO_DELTA, YT_DISPLAY_DEMO_COUNT);
  animateSubscriberDelta(100, 18700);
  animateSubscriberDelta(1000, 123000);
  animateSubscriberDelta(10000, 1230000);
  displayDemoText(formatSubscriberCount(YT_DISPLAY_DEMO_COUNT), 2500);
  displayDemoText(ERROR_TEXT_API_KEY, 2500);
  displayDemoText(ERROR_TEXT_CHANNEL_ID, 2500);
  displayDemoText(ERROR_TEXT_WIFI, 2500);
  displayDemoText(ERROR_TEXT_API_FETCH, 2500);
}

bool fetchSubscriberCount(long* subscriberCount) {
  lastFetchError = FETCH_ERROR_NONE;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Wi-Fi is disconnected."));
    lastFetchError = FETCH_ERROR_WIFI;
    return false;
  }

  if (api.getChannelStatistics(youtubeChannelId)) {
    *subscriberCount = api.channelStats.subscriberCount;
    return true;
  }

  lastFetchError = FETCH_ERROR_API;
  return false;
}

void displayFetchError() {
  if (lastFetchError == FETCH_ERROR_WIFI) {
    displayCenteredText(ERROR_TEXT_WIFI);
    return;
  }

  displayCenteredText(ERROR_TEXT_API_FETCH);
}

void handleFetchSubscribers() {
  long rawSubscriberCount = 0;

  displayCenteredText("Fetch");

  if (fetchSubscriberCount(&rawSubscriberCount)) {
    bool hadPreviousCount = hasLastKnownSubscriberCount;
    long previousSubscriberCount = lastKnownSubscriberCount;
    String formattedSubscriberCount = formatSubscriberCount(rawSubscriberCount);
    Serial.print(F("Subscriber Count: "));
    Serial.println(rawSubscriberCount);

    if (hadPreviousCount && rawSubscriberCount > previousSubscriberCount) {
      long subscriberDelta = rawSubscriberCount - previousSubscriberCount;
      String formattedDelta = formatPublicDisplayStepDelta(subscriberDelta, rawSubscriberCount);
      Serial.print(F("Public Count Step: "));
      Serial.println(formattedDelta);
      animateSubscriberDelta(subscriberDelta, rawSubscriberCount);
    }

    displayCenteredText(formattedSubscriberCount);

    if (!hadPreviousCount || rawSubscriberCount != previousSubscriberCount) {
      lastKnownSubscriberCount = rawSubscriberCount;
      hasLastKnownSubscriberCount = true;
      saveConfig();
    }
  } else {
    Serial.println(F("Failed to fetch subscriber count."));
    displayFetchError();
  }

  lastSubscriberFetch = millis();
}

void loop() {
#if YT_DISPLAY_DEMO
  runDisplayDemo();
  return;
#endif

  updateAccentLeds();

  if (millis() - lastSubscriberFetch >= SUBSCRIBER_FETCH_INTERVAL_MS) {
    handleFetchSubscribers();
  }
}
