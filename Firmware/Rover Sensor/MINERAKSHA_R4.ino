/* MINERAKSHA Revision 4 - ESP32 #2 bench firmware
   Arduino IDE: ESP32 Dev Module / ESP32-WROOM, esp32 by Espressif 3.3.0.
   Libraries: WebSockets 2.7.2, ArduinoJson 7.4.2,
              DHT sensor library 1.4.6 (+ Adafruit Unified Sensor 1.1.15).
   Edit Config.h, upload, then open Serial Monitor at 115200.
   Dashboard http://<ESP32-IP>/ ; one WebSocket ws://<ESP32-IP>:81/
   Raw gas measurements only. No ppm calibration or motor safety permission.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <DHT.h>
#include "Config.h"
#include "Radar.h"
#include "Audio.h"
#include "RoverWiFi.h"
#include "Dashboard.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "Select classic ESP32 Dev Module; this pin map is not for S2/S3/C3."
#endif

WebServer http(80);
WebSocketsServer ws(81);
HardwareSerial radarUART(2);
Radar radar(radarUART);
DHT dht(DHT_PIN, DHT22);

struct GasReading { int16_t raw = 0; float volts = 0; uint32_t at = 0; bool valid = false; };
GasReading gases[3];
const char *gasNames[3] = {"mq135", "mq2", "mq7"};
bool adsOnline = false, adsConverting = false, dhtValid = false;
int adcChannel = 0;
uint32_t adcStarted = 0, adcCycle = 0, lastDht = 0;
float temperature = NAN, humidity = NAN;
uint32_t dhtAt = 0, adcErrors = 0;
uint8_t audioOwner = 255;
uint32_t audioKeepAlive = 0;
bool victimConfirmed = false;
uint32_t victimAt = 0;

bool adsWrite(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(0x48); Wire.write(reg); Wire.write(value >> 8); Wire.write(value & 0xFF);
  return Wire.endTransmission() == 0;
}
bool adsRead(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(0x48); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)0x48, (uint8_t)2) != 2) return false;
  value = ((uint16_t)Wire.read() << 8) | Wire.read(); return true;
}
void adcFailure() {
  ++adcErrors; adsOnline = adsConverting = false;
  for (auto &gas : gases) gas.valid = false;
  adcChannel = 0; adcCycle = millis();
}
void sampleGases() {
  if (!adsConverting) {
    if (adcChannel == 0 && millis() - adcCycle < 500) return;
    // Single-ended A0/A1/A2, PGA +/-4.096V (125uV/count), 128 SPS.
    // PGA full scale does NOT allow the input to exceed the 3.3V supply.
    uint16_t config = 0xC383 | (adcChannel << 12);
    if (!adsWrite(1, config)) { adcFailure(); return; }
    adcStarted = millis(); adsConverting = true; return;
  }
  if (millis() - adcStarted < 10) return;
  uint16_t config, raw;
  if (!adsRead(1, config)) { adcFailure(); return; }
  if (!(config & 0x8000)) {
    if (millis() - adcStarted > 80) adcFailure();
    return;
  }
  if (!adsRead(0, raw)) { adcFailure(); return; }
  auto &gas = gases[adcChannel];
  gas.raw = (int16_t)raw; gas.volts = gas.raw * 0.000125f;
  gas.at = millis(); gas.valid = true; adsOnline = true; adsConverting = false;
  if (++adcChannel == 3) { adcChannel = 0; adcCycle = millis(); }
}
void sampleClimate() {
  if (millis() - lastDht < 2500) return;
  lastDht = millis();
  float h = dht.readHumidity(), t = dht.readTemperature();
  dhtValid = isfinite(h) && isfinite(t) && h >= 0 && h <= 100 && t >= -40 && t <= 80;
  if (dhtValid) { humidity = h; temperature = t; dhtAt = millis(); }
  else { humidity = temperature = NAN; }
}
void reply(uint8_t client, const char *command, bool ok, const String &message) {
  JsonDocument j; j["type"] = "ack"; j["command"] = command; j["ok"] = ok; j["message"] = message;
  String text; serializeJson(j, text); ws.sendTXT(client, text);
  Serial.println(text);
}
void releaseAudio() { stopAudio(); audioOwner = 255; }

void handleCommand(uint8_t client, const uint8_t *payload, size_t length) {
  if (length > 768) { reply(client, "invalid", false, "command too long"); return; }
  JsonDocument j;
  if (deserializeJson(j, payload, length)) { reply(client, "invalid", false, "invalid JSON"); return; }
  const char *command = j["cmd"] | "";
  if (!strcmp(command, "ping")) {
    if (audioOwner == client) audioKeepAlive = millis();
    return;
  }
  if (!strcmp(command, "audio")) {
    const char *mode = j["mode"] | "";
    if (strcmp(mode, "idle") && strcmp(mode, "listen") && strcmp(mode, "talk") && strcmp(mode, "tone")) {
      reply(client, command, false, "mode must be idle/listen/talk/tone"); return;
    }
    if (audioOwner != 255 && audioOwner != client) { reply(client, command, false, "audio is in use by another dashboard"); return; }
    if (!strcmp(mode, "idle")) { releaseAudio(); reply(client, command, true, "idle"); return; }
    if (!audioReady) { reply(client, command, false, "I2S initialization failed; see Serial Monitor"); return; }
    stopAudio(); audioOwner = client; audioKeepAlive = millis(); lastAudioInput.store(millis());
    int nextMode = !strcmp(mode, "listen") ? 1 : (!strcmp(mode, "talk") ? 2 : 3);
    if (nextMode == 3) toneUntil.store(millis() + 600);
    audioMode.store(nextMode); reply(client, command, true, mode);
  } else if (!strcmp(command, "volume")) {
    if (!j["value"].is<int>() || j["value"].as<int>() < 0 || j["value"].as<int>() > 100) {
      reply(client, command, false, "volume must be 0..100"); return;
    }
    speakerVolume.store(j["value"].as<int>()); reply(client, command, true, "volume updated");
  } else if (!strcmp(command, "radar_read")) {
    bool ok = radar.initialize(); reply(client, command, ok, ok ? "reading; watch radar status" : "radar is busy");
  } else if (!strcmp(command, "radar_config")) {
    if (!j["min_gate"].is<int>() || !j["max_gate"].is<int>() || !j["hold_s"].is<int>() || !j["sensitivity"].is<int>()) {
      reply(client, command, false, "integer min_gate/max_gate/hold_s/sensitivity required"); return;
    }
    bool ok = radar.apply(j["min_gate"], j["max_gate"], j["hold_s"], j["sensitivity"]);
    reply(client, command, ok, ok ? "accepted; wait for verified readback" : "invalid range or configuration unavailable/busy");
  } else if (!strcmp(command, "radar_power")) {
    reply(client, command, false, "RF transmit-power control is not verified for this LD2420 protocol; no command sent");
  } else if (!strcmp(command, "victim")) {
    if (!j["confirmed"].is<bool>()) { reply(client, command, false, "confirmed must be true/false"); return; }
    victimConfirmed = j["confirmed"]; victimAt = millis();
    reply(client, command, true, victimConfirmed ? "victim marked by operator" : "operator victim mark cleared");
  } else reply(client, command, false, "unknown command");
}

void socketEvent(uint8_t client, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_DISCONNECTED) {
    if (audioOwner == client) releaseAudio();
    Serial.printf("[WS] client %u disconnected\n", client);
  } else if (type == WStype_CONNECTED) {
    Serial.printf("[WS] client %u connected\n", client);
    reply(client, "hello", true, "JSON telemetry + PCM16LE mono 16000Hz; binary prefix 1=microphone, 2=speaker");
  } else if (type == WStype_TEXT) handleCommand(client, payload, length);
  else if (type == WStype_BIN) {
    // Exactly one audio block, bounded memory. Fragmented frames are unsupported.
    if (!audioReady || client != audioOwner || audioMode.load() != 2) return;
    if (length != 1 + 2 * AUDIO_SAMPLES || payload[0] != 2) { ++speakerDrops; return; }
    AudioFrame f; memcpy(f.samples, payload + 1, sizeof(f.samples));
    lastAudioInput.store(millis());
    if (xQueueSend(speakerQueue, &f, 0) != pdTRUE) ++speakerDrops;
  }
}

String telemetry() {
  JsonDocument j; uint32_t now = millis();
  j["type"] = "telemetry"; j["revision"] = 4; j["uptime_ms"] = now;
  j["firmware_build"] = "r4-wifi-fix1";
  j["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
  j["wifi"]["ip"] = WiFi.localIP().toString(); j["wifi"]["rssi_dbm"] = WiFi.RSSI();
  j["wifi"]["status_code"] = (int)WiFi.status();
  j["wifi"]["attempts"] = wifiAttempts;
  j["wifi"]["last_disconnect_reason"] = wifiLastReason.load();
  j["free_heap"] = ESP.getFreeHeap(); j["ads1115_online"] = adsOnline; j["adc_errors"] = adcErrors;
  for (int i = 0; i < 3; ++i) {
    JsonObject g = j["gas"][gasNames[i]].to<JsonObject>();
    bool valid = gases[i].valid && now - gases[i].at < 2000;
    g["valid_adc_reading"] = valid; g["calibrated"] = false;
    g["ppm"] = nullptr; g["warmup_validated"] = false;
    if (valid) {
      g["raw"] = gases[i].raw; g["adc_v"] = gases[i].volts;
      g["ao_estimate_v"] = gases[i].volts / DIVIDER_RATIO;
      g["input_near_rail"] = gases[i].volts >= ADS_SUPPLY_V - 0.15f;
      g["age_ms"] = now - gases[i].at;
    } else { g["raw"] = nullptr; g["adc_v"] = nullptr; g["ao_estimate_v"] = nullptr; }
  }
  j["gas"]["mq7"]["heater_cycle_verified"] = false;
  j["climate"]["valid"] = dhtValid && now - dhtAt < 6000;
  if (dhtValid) { j["climate"]["temperature_c"] = temperature; j["climate"]["humidity_pct"] = humidity; }
  else { j["climate"]["temperature_c"] = nullptr; j["climate"]["humidity_pct"] = nullptr; }
  JsonObject r = j["radar"].to<JsonObject>();
  r["fresh"] = radar.fresh(); r["config_valid"] = radar.configValid; r["busy"] = radar.busy;
  r["status"] = radar.status; r["firmware"] = radar.firmware; r["baud"] = RADAR_BAUD;
  if (radar.fresh()) r["presence"] = radar.present; else r["presence"] = nullptr;
  if (radar.distanceFresh()) r["distance_cm"] = radar.distanceCm; else r["distance_cm"] = nullptr;
  r["reports"] = radar.goodReports; r["bad_frames"] = radar.badFrames;
  r["report_age_ms"] = now - radar.lastReport;
  if (radar.configValid) {
    r["min_gate"] = radar.minGate; r["max_gate"] = radar.maxGate; r["hold_s"] = radar.holdSeconds;
    r["sensitivity"] = radar.sensitivity;
    for (int g = 0; g < 16; ++g) { r["move_thresholds"].add(radar.move[g]); r["still_thresholds"].add(radar.still[g]); }
  }
  r["rf_power_control_supported"] = false;
  j["victim"]["operator_confirmed"] = victimConfirmed; j["victim"]["marked_at_ms"] = victimAt;
  j["victim"]["automatic_identification"] = false;
  JsonObject a = j["audio"].to<JsonObject>();
  a["initialized"] = audioReady; a["mode"] = audioMode.load(); a["sample_rate"] = AUDIO_RATE;
  a["mic_peak"] = microphonePeak.load(); a["volume"] = speakerVolume.load();
  a["mic_dropped_blocks"] = micDrops.load(); a["speaker_dropped_blocks"] = speakerDrops.load(); a["i2s_errors"] = audioErrors.load();
  // Status only. Permit/heartbeat GPIOs were not specified in the supplied table.
  j["safety"]["run_permit"] = false; j["safety"]["gas_validated"] = false;
  j["safety"]["hardware_interlock"] = "not implemented: pins not supplied";
  String text; text.reserve(2600); serializeJson(j, text); return text;
}

void setup() {
  Serial.begin(115200); delay(300);
  Serial.println("\nMINERAKSHA R4 / ESP32 #2 / bench telemetry and audio");
  // The SX1278 is wired but unused in this Wi-Fi test: deselect and hold reset.
  pinMode(5, OUTPUT); digitalWrite(5, HIGH); pinMode(14, OUTPUT); digitalWrite(14, LOW);
  Wire.begin(SDA_PIN, SCL_PIN, 100000); Wire.setTimeOut(25);
  dht.begin(); radar.begin(RADAR_BAUD, RADAR_RX, RADAR_TX);
  audioReady = beginAudio(); Serial.printf("[AUDIO] initialization: %s\n", audioReady ? "OK" : "FAILED");
  beginNetwork();
  http.on("/", []() { http.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML); });
  http.onNotFound([]() { http.send(404, "text/plain", "Open /"); });
  http.begin(); ws.begin(); ws.onEvent(socketEvent); ws.enableHeartbeat(15000, 3000, 2);
  Serial.println("[WIFI] Connecting. Dashboard and WebSocket addresses will appear here.");
  Serial.println("[GAS] Raw/voltage only. MQ-7 heater control and ppm calibration are not implemented.");
}

void loop() {
  static uint32_t lastTelemetry = 0, lastSerial = 0;
  static bool radarStarted = false, wasConnected = false;
  static String lastIP;
  uint32_t now = millis();
  if (!radarStarted && now > 2500) { radar.initialize(); radarStarted = true; }
  radar.tick(); sampleGases(); sampleClimate();
  http.handleClient(); ws.loop();
  serviceNetwork();
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && (!wasConnected || lastIP != WiFi.localIP().toString())) {
    lastIP = WiFi.localIP().toString();
    Serial.printf("[WIFI] Dashboard: http://%s/\n[WIFI] WebSocket: ws://%s:81/\n", lastIP.c_str(), lastIP.c_str());
  }
  if (wasConnected && !connected) { releaseAudio(); Serial.println("[WIFI] Disconnected"); }
  wasConnected = connected;
  if (audioOwner != 255 && (now - audioKeepAlive > 6000 || !connected)) releaseAudio();
  if (audioMode.load() == 3 && (int32_t)(now - toneUntil.load()) >= 0) releaseAudio();
  if (audioReady) {
    AudioFrame f;
    if (xQueueReceive(microphoneQueue, &f, 0) == pdTRUE && audioOwner != 255 && audioMode.load() == 1) {
      uint8_t packet[1 + sizeof(f.samples)]; packet[0] = 1; memcpy(packet + 1, f.samples, sizeof(f.samples));
      if (!ws.sendBIN(audioOwner, packet, sizeof(packet))) ++micDrops;
    }
  }
  if (now - lastTelemetry >= 500) {
    lastTelemetry = now; String data = telemetry(); ws.broadcastTXT(data);
    if (now - lastSerial >= 1000) { lastSerial = now; Serial.println(data); }
  }
  delay(1);
}
