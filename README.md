# YT Sub Counter

ESP8266 firmware for showing a live YouTube subscriber count on a MAX7219 LED matrix, with optional NeoPixel accent LEDs. The device uses a setup portal for Wi-Fi, YouTube API key, and channel ID configuration, so private values do not need to be hard-coded into the sketch.

## Features

- Fetches YouTube channel statistics through the YouTube Data API v3.
- Shows the current subscriber count on a 4-module MAX7219 LED matrix.
- Refreshes once on boot and then every 3 minutes from the main loop.
- Shows a wave animation for subscriber changes, then `+NUMBER` or `-NUMBER`, then the updated count.
- Stores Wi-Fi credentials through WiFiManager.
- Stores YouTube API key and channel ID in LittleFS.
- Supports an optional local-only `secrets.h` file for hardware deployments.
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

## Local Hardware Secrets

The repo does not commit private values. For a local hardware deployment, copy the example file and fill in your values:

```sh
cp secrets.example.h secrets.h
```

Then edit `secrets.h`:

```cpp
#define YT_WIFI_SSID "Your Wi-Fi SSID"
#define YT_WIFI_PASSWORD "Your Wi-Fi password"
#define YT_API_KEY "Your YouTube Data API v3 key"
#define YT_CHANNEL_ID "Your YouTube channel ID"
```

`secrets.h` is ignored by git. If it is present, the firmware tries those Wi-Fi credentials first and uses the embedded YouTube API key/channel ID in preference to older saved portal values. If the Wi-Fi connection fails or no local secrets are present, it falls back to the `YouTubePlayButtonSetup` captive portal.

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

The sketch fetches the subscriber count once after setup, then refreshes every 3 minutes. On each successful refresh after the first known count, it shows a short top-and-bottom wave animation with the middle rows left empty, then shows the delta as `+NUMBER` or `-NUMBER`, and then shows the updated subscriber count. The last known count is stored in LittleFS so the delta can survive a restart.

The refresh is driven by `millis()` in `loop()`, which keeps HTTPS/API work out of timer callbacks and avoids the update stall caused by the old loop counter approach.

## API Quota

The firmware uses the YouTube Data API `channels.list` statistics request through the `YoutubeApi` library. Google's quota table lists `channels.list` as a 1-unit request, and the default project quota is 10,000 units per day with daily reset at midnight Pacific Time.

At the default 3-minute refresh interval, one counter uses about 480 units/day. For comparison:

| Refresh interval | Requests/day | Quota units/day |
| --- | ---: | ---: |
| 1 minute | 1,440 | 1,440 |
| 3 minutes | 480 | 480 |
| 5 minutes | 288 | 288 |
| 10 minutes | 144 | 144 |
| 1 hour | 24 | 24 |

The interval is set in `SUBSCRIBER_FETCH_INTERVAL_MS` in `Counter.ino`.

## Troubleshooting

- Display shows `Setup`: the YouTube API key or channel ID is missing.
- Display shows `Error`: Wi-Fi is disconnected, the API request failed, or the API key/channel ID is invalid.
- Need to change saved settings: erase the ESP8266 flash or clear WiFiManager/LittleFS data, then reboot and use the setup portal again.
- PlatformIO build fails after dependency changes: delete `.pio/` and run `platformio run` again.
