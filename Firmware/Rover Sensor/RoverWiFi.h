#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <atomic>
#include "Config.h"

// Own the retry schedule instead of combining auto-reconnect with periodic
// reconnect() calls. The latter can call connect while STA is still connecting.
enum class WifiPhase { Connecting, RetryWait, Online };
WifiPhase wifiPhase = WifiPhase::Connecting;
uint32_t wifiPhaseAt = 0, wifiAttempts = 0;
std::atomic<uint32_t> wifiDisconnectCount{0};
std::atomic<unsigned> wifiLastReason{0};

const char *wifiReasonHint(unsigned reason) {
  switch (reason) {
    case 201: return "AP not found: check exact SSID, 2.4GHz band and signal";
    case 202: return "authentication failed: check password and router security";
    case 15: case 204: return "handshake timed out: check password, security and signal";
    case 200: return "beacons lost: check signal/router availability";
    case 203: return "association failed: router refused or connection failed";
    case 210: case 211: return "no AP with acceptable security settings";
    case 212: return "AP signal below configured threshold";
    case 8: return "station left AP (also expected when cancelling an attempt)";
    default: return "see numeric disconnect reason; cause not yet identified";
  }
}
void wifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  // Callback runs in a different task. Publish atomics; do not change Wi-Fi here.
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    wifiLastReason.store(info.wifi_sta_disconnected.reason);
    ++wifiDisconnectCount;
  }
}
void startWifiAttempt() {
  wifiPhase = WifiPhase::Connecting; wifiPhaseAt = millis(); ++wifiAttempts;
  Serial.printf("[WIFI] Attempt %lu; SSID=%s\n", (unsigned long)wifiAttempts, WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}
void beginNetwork() {
  WiFi.onEvent(wifiEvent);
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.setAutoReconnect(false);
  startWifiAttempt();
}
void cancelWifiAttempt() {
  // Arduino 3.3.0's STA.disconnect() can return early if not yet associated.
  // Call the IDF cancellation API directly so an in-progress attempt is stopped.
  esp_err_t result = esp_wifi_disconnect();
  Serial.printf("[WIFI] Cancel attempt: %s. Retry in 2 seconds.\n", esp_err_to_name(result));
  wifiPhase = WifiPhase::RetryWait; wifiPhaseAt = millis();
}
void serviceNetwork() {
  static uint32_t loggedDisconnects = 0;
  uint32_t count = wifiDisconnectCount.load();
  if (count != loggedDisconnects) {
    loggedDisconnects = count; unsigned reason = wifiLastReason.load();
    Serial.printf("[WIFI] Disconnect reason %u: %s\n", reason, wifiReasonHint(reason));
  }
  if (WiFi.status() == WL_CONNECTED) { wifiPhase = WifiPhase::Online; return; }
  uint32_t elapsed = millis() - wifiPhaseAt;
  if (wifiPhase == WifiPhase::Online) cancelWifiAttempt();
  else if (wifiPhase == WifiPhase::Connecting && elapsed >= 20000) {
    Serial.printf("[WIFI] No IP after 20 seconds; status=%d. Check credentials, 2.4GHz signal and router DHCP.\n", WiFi.status());
    cancelWifiAttempt();
  } else if (wifiPhase == WifiPhase::RetryWait && elapsed >= 2000) startWifiAttempt();
}
