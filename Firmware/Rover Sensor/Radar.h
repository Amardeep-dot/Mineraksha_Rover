#pragma once
#include <Arduino.h>
#include <math.h>

// Independent implementation of the LD2420 UART command protocol.
// Reference: ESPHome's LD2420 driver (see README source links).
// All transactions are nonblocking and have matching ACK/error/length checks.
class Radar {
 public:
  explicit Radar(HardwareSerial &port) : uart(port) {}
  bool busy = false, configValid = false, presenceSeen = false, present = false;
  bool rangeSeen = false;
  uint16_t distanceCm = 0;
  uint32_t lastReport = 0, lastRange = 0, goodReports = 0, badFrames = 0;
  uint32_t minGate = 0, maxGate = 0, holdSeconds = 0;
  uint32_t move[16] = {}, still[16] = {};
  int sensitivity = 50; // Relative to thresholds read at initialization.
  char firmware[24] = "unknown";
  String status = "not initialized";

  void begin(uint32_t baud, int rx, int tx) {
    uart.setRxBufferSize(1024);
    uart.begin(baud, SERIAL_8N1, rx, tx);
  }
  bool fresh() const { return !busy && presenceSeen && millis() - lastReport < 3000; }
  bool distanceFresh() const { return fresh() && present && rangeSeen && millis() - lastRange < 3000; }
  bool initialize() {
    if (busy) return false;
    applying = false; return startJob();
  }
  bool apply(uint32_t lo, uint32_t hi, uint32_t hold, int sense) {
    if (busy || !configValid || lo >= hi || hi > 15 || hold < 1 || hold > 120 || sense < 0 || sense > 100) return false;
    wantedMin = lo; wantedMax = hi; wantedHold = hold; wantedSense = sense;
    float factor = powf(2.0f, (50.0f - sense) / 25.0f);
    for (int i = 0; i < 16; ++i) {
      wantedMove[i] = constrain((uint32_t)lroundf(baseMove[i] * factor), 1UL, 65535UL);
      wantedStill[i] = constrain((uint32_t)lroundf(baseStill[i] * factor), 1UL, 65535UL);
    }
    applying = true; return startJob();
  }
  void tick() {
    // Reset a truncated frame, including when UART is completely silent.
    if (used && millis() - lastByte > 150) { used = 0; ++badFrames; }
    for (int budget = 0; budget < 512 && uart.available(); ++budget) feed((uint8_t)uart.read());
    if (busy && waiting && millis() - sentAt > 700) {
      if (aborting) finish(false, "UART timeout; check baud/TX pin; power-cycle radar if needed");
      else fail("command timeout");
    }
    if (busy && !waiting) next();
  }

 private:
  HardwareSerial &uart;
  uint8_t frame[160] = {}, line[64] = {};
  size_t used = 0, lineUsed = 0, expected = 0;
  uint32_t lastByte = 0, sentAt = 0;
  bool waiting = false, applying = false, aborting = false;
  int step = 0;
  uint16_t pending = 0;
  uint32_t baseMove[16] = {}, baseStill[16] = {};
  uint32_t wantedMove[16] = {}, wantedStill[16] = {};
  uint32_t wantedMin = 0, wantedMax = 0, wantedHold = 0;
  int wantedSense = 50;
  uint32_t readMin = 0, readMax = 0, readHold = 0;
  uint32_t readMove[16] = {}, readStill[16] = {};
  String failure;
  static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
  static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p + 2) << 16); }
  static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
  static void parameter(uint8_t *p, uint16_t reg, uint32_t v) {
    put16(p, reg); put16(p + 2, v); put16(p + 4, v >> 16);
  }
  bool startJob() {
    busy = true; configValid = false; waiting = aborting = false;
    presenceSeen = rangeSeen = false; step = 0; used = lineUsed = 0;
    status = applying ? "applying; awaiting readback" : "reading radar configuration";
    return true;
  }
  void send(uint16_t command, const uint8_t *data = nullptr, size_t n = 0) {
    uint8_t packet[64] = {0xFD, 0xFC, 0xFB, 0xFA};
    put16(packet + 4, n + 2); put16(packet + 6, command);
    if (n) memcpy(packet + 8, data, n);
    const uint8_t tail[4] = {4, 3, 2, 1}; memcpy(packet + 8 + n, tail, 4);
    pending = command; waiting = true; sentAt = millis();
    uart.write(packet, n + 12);
  }
  void finish(bool ok, const String &message) {
    busy = waiting = false; configValid = ok;
    presenceSeen = rangeSeen = false; status = message;
    Serial.print("[RADAR] "); Serial.println(status);
  }
  void fail(const String &message) {
    if (aborting) { finish(false, failure + "; exit failed; power-cycle radar"); return; }
    failure = message; aborting = true; waiting = false;
    send(0x00FE); // Always attempt to leave configuration mode after a failure.
  }
  void next() {
    uint8_t data[20] = {};
    // Common phases: 0 enter, 1 firmware, 2 simple reporting, 3 write globals,
    // 4..19 write thresholds, 20 read globals, 21..36 read thresholds, 37 exit.
    if (step == 0) { put16(data, 2); send(0x00FF, data, 2); }
    else if (step == 1) { send(0x0000); }
    else if (step == 2) { parameter(data, 0, 0x64); send(0x0012, data, 6); }
    else if (step == 3) {
      if (!applying) { step = 20; return; }
      parameter(data, 0, wantedMin); parameter(data + 6, 1, wantedMax); parameter(data + 12, 4, wantedHold);
      send(0x0007, data, 18);
    } else if (step >= 4 && step <= 19) {
      int g = step - 4; parameter(data, 0x10 + g, wantedMove[g]); parameter(data + 6, 0x20 + g, wantedStill[g]);
      send(0x0007, data, 12);
    } else if (step == 20) {
      put16(data, 0); put16(data + 2, 1); put16(data + 4, 4); send(0x0008, data, 6);
    } else if (step >= 21 && step <= 36) {
      int g = step - 21; put16(data, 0x10 + g); put16(data + 2, 0x20 + g); send(0x0008, data, 4);
    } else if (step == 37) { send(0x00FE); }
  }
  void ack(size_t body) {
    if (!busy || !waiting || u16(frame + 6) != (pending | 0x0100)) return;
    waiting = false;
    if (u16(frame + 8) != 0) { fail("radar rejected command 0x" + String(pending, HEX)); return; }
    if (aborting) { finish(false, failure + "; configuration unknown; retry Read radar"); return; }
    if (step == 1) {
      if (body < 6) { fail("short firmware reply"); return; }
      size_t n = u16(frame + 10);
      if (n > body - 6) { fail("invalid firmware reply length"); return; }
      n = min(n, sizeof(firmware) - 1); memcpy(firmware, frame + 12, n); firmware[n] = 0;
    } else if (step == 20) {
      if (body != 16) { fail("invalid gate readback length"); return; }
      readMin = u32(frame + 10); readMax = u32(frame + 14); readHold = u32(frame + 18);
      if (readMin > readMax || readMax > 15 || readHold > 65535) { fail("invalid gate values"); return; }
      if (applying && (readMin != wantedMin || readMax != wantedMax || readHold != wantedHold)) { fail("range readback mismatch"); return; }
    } else if (step >= 21 && step <= 36) {
      if (body != 12) { fail("invalid threshold readback length"); return; }
      int g = step - 21; readMove[g] = u32(frame + 10); readStill[g] = u32(frame + 14);
      if (readMove[g] > 65535 || readStill[g] > 65535) { fail("unexpected threshold values"); return; }
      if (applying && (readMove[g] != wantedMove[g] || readStill[g] != wantedStill[g])) { fail("threshold readback mismatch"); return; }
    } else if (step == 37) {
      minGate = readMin; maxGate = readMax; holdSeconds = readHold;
      for (int g = 0; g < 16; ++g) {
        move[g] = readMove[g]; still[g] = readStill[g];
        if (!applying) { baseMove[g] = move[g]; baseStill[g] = still[g]; }
      }
      sensitivity = applying ? wantedSense : 50;
      finish(true, "ready; configuration readback verified"); return;
    }
    ++step;
  }
  void textByte(uint8_t b) {
    if (b == '\r') return;
    if (b == '\n') {
      line[lineUsed] = 0;
      char *s = (char *)line;
      if (!strcmp(s, "ON") || !strcmp(s, "OFF")) {
        present = !strcmp(s, "ON"); presenceSeen = true; lastReport = millis(); ++goodReports;
        if (!present) rangeSeen = false;
      } else if (!strncmp(s, "Range ", 6)) {
        char *end; long cm = strtol(s + 6, &end, 10);
        if (end != s + 6 && *end == 0 && cm >= 0 && cm <= 2000) {
          distanceCm = cm; rangeSeen = true; lastRange = lastReport = millis(); ++goodReports;
        }
      }
      lineUsed = 0;
    } else if (b >= 32 && b <= 126 && lineUsed < sizeof(line) - 1) line[lineUsed++] = b;
    else lineUsed = 0;
  }
  void feed(uint8_t b) {
    lastByte = millis();
    if (!used && b != 0xFD && b != 0xF4) { textByte(b); return; }
    if (!used) lineUsed = 0;
    frame[used++] = b;
    if (used <= 4) {
      const uint8_t h1[4] = {0xFD,0xFC,0xFB,0xFA}, h2[4] = {0xF4,0xF3,0xF2,0xF1};
      const uint8_t *h = frame[0] == 0xFD ? h1 : h2;
      if (b != h[used - 1]) { used = 0; ++badFrames; if (b == 0xFD || b == 0xF4) frame[used++] = b; }
      return;
    }
    if (used == 6) {
      size_t body = u16(frame + 4); expected = body + 10;
      if (expected > sizeof(frame) || body < 4 || (frame[0] == 0xF4 && body != 35)) { used = 0; ++badFrames; }
      return;
    }
    if (used >= 6 && used == expected) {
      const uint8_t t1[4] = {4,3,2,1}, t2[4] = {0xF8,0xF7,0xF6,0xF5};
      const uint8_t *tail = frame[0] == 0xFD ? t1 : t2;
      if (memcmp(frame + expected - 4, tail, 4)) ++badFrames;
      else if (frame[0] == 0xFD) ack(expected - 10);
      else if (frame[6] <= 1) {
        present = frame[6]; distanceCm = u16(frame + 7);
        presenceSeen = true; rangeSeen = present; lastReport = lastRange = millis(); ++goodReports;
      }
      used = 0;
    }
  }
};
