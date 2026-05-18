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
const unsigned long DELTA_TEXT_DISPLAY_MS = 1600UL;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
const uint8_t DISPLAY_COLUMN_COUNT = MAX_DEVICES * 8;
const uint8_t WAVE_FRAME_COUNT = 18;
const uint8_t WAVE_FRAME_DELAY_MS = 75;

char youtubeApiKey[80] = "";
char youtubeChannelId[48] = "";
bool shouldSaveConfig = false;
unsigned long lastSubscriberFetch = 0;
long lastKnownSubscriberCount = -1;
bool hasLastKnownSubscriberCount = false;

Adafruit_NeoPixel leds = Adafruit_NeoPixel(LED_NUM, PIN, NEO_GRB + NEO_KHZ800);
MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

WiFiClientSecure client;
YoutubeApi api(youtubeApiKey, client);

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
  myDisplay.displayClear();
  myDisplay.print("WiFi");

  WiFi.begin(YT_WIFI_SSID, YT_WIFI_PASSWORD);
  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    yield();
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
    myDisplay.displayClear();
    myDisplay.print(hasConfig ? "WiFi" : "Setup");

    connected = hasConfig
      ? wifiManager.autoConnect(CONFIG_PORTAL_SSID)
      : wifiManager.startConfigPortal(CONFIG_PORTAL_SSID);
  }

  if (!connected) {
    Serial.println(F("Failed to connect or configure Wi-Fi."));
    myDisplay.displayClear();
    myDisplay.print("Retry");
    delay(3000);
    ESP.restart();
  }

  bool configChanged = false;
  configChanged |= copyConfigValue(apiKeyParameter.getValue(), youtubeApiKey, sizeof(youtubeApiKey));
  configChanged |= copyConfigValue(channelIdParameter.getValue(), youtubeChannelId, sizeof(youtubeChannelId));

  if (shouldSaveConfig || configChanged || embeddedConfigChanged) {
    saveConfig();
  }

  if (strlen(youtubeApiKey) == 0 || strlen(youtubeChannelId) == 0) {
    Serial.println(F("Missing YouTube API key or channel ID."));
    myDisplay.displayClear();
    myDisplay.print("Setup");
    delay(3000);
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
  myDisplay.displayClear();
  myDisplay.print("Hello");

  delay(1000);

#if YT_DISPLAY_DEMO
  return;
#endif

  connectWifiAndLoadConfig();

  myDisplay.displayClear();
  myDisplay.print("Done!");

  client.setInsecure();
  handleFetchSubscribers();
}

void led_set(uint8_t R, uint8_t G, uint8_t B) {
  for (int i = 0; i < LED_NUM; i++) {
    leds.setPixelColor(i, leds.Color(R, G, B));
    leds.show();
    delay(66);
  }
}

String formatSubscriberCount(long count) {
  if (count < 100000) {
    return String(count);
  } else if (count < 1000000) {
    return String(count / 1000) + "K";
  } else if (count < 10000000) {
    int millions = count / 1000000;
    int remainder = (count % 1000000) / 100000;
    return String(millions) + "." + String(remainder) + "M";
  } else {
    int millions = count / 1000000;
    return String(millions) + "M";
  }
}

String formatSubscriberDelta(long delta) {
  long absoluteDelta = labs(delta);
  String value;

  if (absoluteDelta < 1000) {
    value = String(absoluteDelta);
  } else if (absoluteDelta < 1000000) {
    value = String(absoluteDelta / 1000) + "K";
  } else {
    value = String(absoluteDelta / 1000000) + "M";
  }

  if (delta > 0) {
    return "+" + value;
  }

  if (delta < 0) {
    return "-" + value;
  }

  return value;
}

void drawDeltaWaveFrame(uint8_t phase) {
  static const uint8_t waveRows[] = {1, 0, 0, 1, 2, 2, 1, 0};
  MD_MAX72XX* matrix = myDisplay.getGraphicObject();

  matrix->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  matrix->clear();

  for (uint8_t col = 0; col < DISPLAY_COLUMN_COUNT; col++) {
    uint8_t topRow = waveRows[(col + phase) % (sizeof(waveRows) / sizeof(waveRows[0]))];
    matrix->setPoint(topRow, col, true);
    matrix->setPoint(7 - topRow, col, true);
  }

  matrix->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void animateSubscriberDelta(long delta) {
  String formattedDelta = formatSubscriberDelta(delta);

  for (uint8_t frame = 0; frame < WAVE_FRAME_COUNT; frame++) {
    drawDeltaWaveFrame(frame);
    delay(WAVE_FRAME_DELAY_MS);
    yield();
  }

  myDisplay.displayClear();
  myDisplay.print(formattedDelta);
  delay(DELTA_TEXT_DISPLAY_MS);
}

bool fetchSubscriberCount(long* subscriberCount) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Wi-Fi is disconnected."));
    return false;
  }

  if (api.getChannelStatistics(youtubeChannelId)) {
    *subscriberCount = api.channelStats.subscriberCount;
    return true;
  }

  return false;
}

void handleFetchSubscribers() {
  long rawSubscriberCount = 0;

  myDisplay.displayClear();
  myDisplay.print("Fetch");

  if (fetchSubscriberCount(&rawSubscriberCount)) {
    bool hadPreviousCount = hasLastKnownSubscriberCount;
    long previousSubscriberCount = lastKnownSubscriberCount;
    String formattedSubscriberCount = formatSubscriberCount(rawSubscriberCount);
    Serial.print(F("Subscriber Count: "));
    Serial.println(rawSubscriberCount);

    if (hadPreviousCount) {
      long subscriberDelta = rawSubscriberCount - previousSubscriberCount;
      String formattedDelta = formatSubscriberDelta(subscriberDelta);
      Serial.print(F("Subscriber Delta: "));
      Serial.println(formattedDelta);
      animateSubscriberDelta(subscriberDelta);
    }

    myDisplay.displayClear();
    myDisplay.print(formattedSubscriberCount);

    if (!hadPreviousCount || rawSubscriberCount != previousSubscriberCount) {
      lastKnownSubscriberCount = rawSubscriberCount;
      hasLastKnownSubscriberCount = true;
      saveConfig();
    }
  } else {
    Serial.println(F("Failed to fetch subscriber count."));
    myDisplay.displayClear();
    myDisplay.print("Error");
  }

  lastSubscriberFetch = millis();
}

void loop() {
#if YT_DISPLAY_DEMO
  animateSubscriberDelta(YT_DISPLAY_DEMO_DELTA);
  myDisplay.displayClear();
  myDisplay.print(formatSubscriberCount(YT_DISPLAY_DEMO_COUNT));
  delay(2500);
  return;
#endif

  led_set(50, 50, 50);
  led_set(80, 80, 100);

  led_set(50, 50, 50);
  led_set(100, 80, 80);

  led_set(50, 50, 50);
  led_set(80, 100, 80);

  if (millis() - lastSubscriberFetch >= SUBSCRIBER_FETCH_INTERVAL_MS) {
    handleFetchSubscribers();
  }
}
