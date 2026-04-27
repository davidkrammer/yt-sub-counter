# YT Sub Counter

ESP8266 firmware for showing a live YouTube subscriber count on a MAX7219 LED matrix, with optional NeoPixel accent LEDs. The device uses a setup portal for Wi-Fi, YouTube API key, and channel ID configuration, so private values do not need to be hard-coded into the sketch.

## Features

- Fetches YouTube channel statistics through the YouTube Data API v3.
- Shows the current subscriber count on a 4-module MAX7219 LED matrix.
- Refreshes once on boot and then every hour from the main loop.
- Stores Wi-Fi credentials through WiFiManager.
- Stores YouTube API key and channel ID in LittleFS.
- Includes a PlatformIO project file for repeatable builds.

## Hardware

The default pin mapping matches a NodeMCU-style ESP8266 board:

| Part | ESP8266 pin |
| --- | --- |
| MAX7219 CLK | `D6` |
| MAX7219 DATA | `D8` |
| MAX7219 CS | `D7` |
| NeoPixel data | `D4` |

The sketch is configured for four FC-16 MAX7219 matrix modules and three NeoPixel LEDs. Change `MAX_DEVICES` or `LED_NUM` in `Counter.ino` if your hardware differs.

## Requirements

- ESP8266 board package
- YouTube Data API v3 key
- YouTube channel ID
- Arduino IDE or PlatformIO

Libraries:

- ESP8266WiFi
- WiFiClientSecure
- LittleFS
- WiFiManager
- MD_Parola
- MD_MAX72xx
- YoutubeApi
- ArduinoJson
- Adafruit_NeoPixel

PlatformIO installs the project dependencies from `platformio.ini` automatically.

## YouTube Setup

1. Create a project in [Google Cloud Console](https://console.cloud.google.com/).
2. Open **API & Services**.
3. Select **Enabled APIs & services**.
4. Click **+ Enable APIs and Services**.
5. Search for **YouTube Data API v3**.
6. Enable the API.
7. Open **Credentials**.
8. Click **+ Create Credentials** and choose **API key**.
9. Find your YouTube channel ID using [Google's channel ID guide](https://support.google.com/youtube/answer/3250431?hl=en).

Keep the API key and channel ID ready for the device setup portal.

## Build and Upload

With PlatformIO:

```sh
platformio run
platformio run --target upload
```

With Arduino IDE:

1. Open `Counter.ino`.
2. Select your ESP8266 board and port.
3. Install the libraries listed above through Library Manager.
4. Upload the sketch.

## First Boot Configuration

1. Power on the ESP8266.
2. Connect your phone or computer to the Wi-Fi network named `YouTubePlayButtonSetup`.
3. Open the captive portal if it does not appear automatically.
4. Enter your Wi-Fi network, Wi-Fi password, YouTube API key, and YouTube channel ID.
5. Save the form.

The ESP8266 stores the values locally and reuses them on future boots. If saved Wi-Fi credentials stop working, WiFiManager opens the setup portal again.

## Refresh Behavior

The sketch fetches the subscriber count once after setup, then refreshes every hour. The refresh is driven by `millis()` in `loop()`, which keeps HTTPS/API work out of timer callbacks and avoids the update stall caused by the old loop counter approach.

## Troubleshooting

- Display shows `Setup`: the YouTube API key or channel ID is missing.
- Display shows `Error`: Wi-Fi is disconnected, the API request failed, or the API key/channel ID is invalid.
- Need to change saved settings: erase the ESP8266 flash or clear WiFiManager/LittleFS data, then reboot and use the setup portal again.
- PlatformIO build fails after dependency changes: delete `.pio/` and run `platformio run` again.
