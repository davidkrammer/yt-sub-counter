#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <WiFiManager.h>

#include <YoutubeApi.h>
#include <ArduinoJson.h>

#include <Adafruit_NeoPixel.h>

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
const unsigned long SUBSCRIBER_FETCH_INTERVAL_MS = 60UL * 60UL * 1000UL;

char youtubeApiKey[80] = "";
char youtubeChannelId[48] = "";
bool shouldSaveConfig = false;
unsigned long lastSubscriberFetch = 0;

Adafruit_NeoPixel leds = Adafruit_NeoPixel(LED_NUM, PIN, NEO_GRB + NEO_KHZ800);
MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

WiFiClientSecure client;
YoutubeApi api(youtubeApiKey, client);

void saveConfigCallback() {
  shouldSaveConfig = true;
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

  return strlen(youtubeApiKey) > 0 && strlen(youtubeChannelId) > 0;
}

bool saveConfig() {
  if (!initializeFileSystem()) {
    return false;
  }

  StaticJsonDocument<256> doc;
  doc["apiKey"] = youtubeApiKey;
  doc["channelId"] = youtubeChannelId;

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
  if (value == nullptr || strlen(value) == 0) {
    return false;
  }

  if (strncmp(value, target, targetSize) == 0) {
    return false;
  }

  strlcpy(target, value, targetSize);
  return true;
}

void connectWifiAndLoadConfig() {
  bool hasConfig = loadConfig();

  WiFi.mode(WIFI_STA);

  WiFiManager wifiManager;
  wifiManager.setSaveParamsCallback(saveConfigCallback);

  WiFiManagerParameter apiKeyParameter("api_key", "YouTube API key", youtubeApiKey, sizeof(youtubeApiKey));
  WiFiManagerParameter channelIdParameter("channel_id", "YouTube Channel ID", youtubeChannelId, sizeof(youtubeChannelId));
  wifiManager.addParameter(&apiKeyParameter);
  wifiManager.addParameter(&channelIdParameter);

  myDisplay.displayClear();
  myDisplay.print(hasConfig ? "WiFi" : "Setup");

  bool connected = hasConfig
    ? wifiManager.autoConnect(CONFIG_PORTAL_SSID)
    : wifiManager.startConfigPortal(CONFIG_PORTAL_SSID);

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

  if (shouldSaveConfig || configChanged) {
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
  if (count < 1000) {
     return String(count);
  } else if (count < 10000) {
     return String(count);
  } else if (count < 100000) {
     int thousands = count / 1000;
     int remainder = (count % 1000) / 10;
     return String(thousands) + "," + (remainder < 10 ? "0" : "") + String(remainder) + "K";
  } else if (count < 1000000) {
     int thousands = count / 1000;
     int remainder = (count % 1000) / 100;
     return String(thousands) + "," + String(remainder) + "K";
  } else if (count < 10000000) {
    int millions = count / 1000000;
    int remainder = (count % 1000000) / 100000;
    return String(millions) + "," + String(remainder) + "M";
  } else {
    int millions = count / 1000000;
    return String(millions) + "M";
  }
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
    String formattedSubscriberCount = formatSubscriberCount(rawSubscriberCount);
    Serial.print(F("Subscriber Count: "));
    Serial.println(rawSubscriberCount);
    myDisplay.displayClear();
    myDisplay.print(formattedSubscriberCount);
  } else {
    Serial.println(F("Failed to fetch subscriber count."));
    myDisplay.displayClear();
    myDisplay.print("Error");
  }

  lastSubscriberFetch = millis();
}

void loop() {
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
