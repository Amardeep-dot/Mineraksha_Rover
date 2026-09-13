#pragma once

// Edit these two lines before uploading. ESP32 uses 2.4 GHz Wi-Fi.
constexpr char WIFI_SSID[] = "Power House";
constexpr char WIFI_PASSWORD[] = "onetwonine";

// Classic ESP32-WROOM Dev Module; Arduino-ESP32 3.3.0.
// GPIO16/17 may be occupied by PSRAM on WROVER boards.
constexpr uint32_t RADAR_BAUD = 115200;
// LD2420 >= 1.5.3: OT1/TX -> GPIO16. Older: OT2 -> GPIO16, 256000 baud.
constexpr int RADAR_RX = 16, RADAR_TX = 17;
constexpr int DHT_PIN = 13;
constexpr int SDA_PIN = 21, SCL_PIN = 22;
constexpr int AUDIO_BCLK = 32, AUDIO_WS = 33;
constexpr int MIC_DATA = 34, SPEAKER_DATA = 25;
constexpr uint32_t AUDIO_RATE = 16000;
constexpr size_t AUDIO_SAMPLES = 256;

// Actual divider: sensor AO -- 10k -- ADS input -- 15k -- GND.
constexpr float DIVIDER_RATIO = 15000.0f / (10000.0f + 15000.0f);
constexpr float ADS_SUPPLY_V = 3.3f; // Replace with your measured 3.3V rail.
