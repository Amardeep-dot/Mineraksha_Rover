/*
  ESP32 Rover Firmware - MINERAKSHA Dashboard Integration
  Integrates:
    - WiFi Station Mode (Connects to router)
    - WebSockets (Port 81) with JSON command/telemetry streaming
    - 4x Ultrasonic Sensors, TB6612FNG Motor Driver, MPU6050
    - Matches exact JSON protocol expected by MINERAKSHA_Dashboard_Final.html
*/

#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// WiFi Configuration
// ---------------------------------------------------------------------------
const char* ssid = "Power House";
const char* password = "onetwonine";

WebSocketsServer webSocket(81);

// ---------------------------------------------------------------------------
// Pin Definitions
// ---------------------------------------------------------------------------
#define FRONT_TRIG   16
#define FRONT_ECHO   34
#define REAR_TRIG    19
#define REAR_ECHO    2
#define LEFT_TRIG    17
#define LEFT_ECHO    39
#define RIGHT_TRIG   18
#define RIGHT_ECHO   13
#define ESTOP_PIN    4

#define PWMA         25
#define AIN1         32
#define AIN2         33
#define PWMB         26
#define BIN1         14
#define BIN2         27
#define STBY         23

#define SDA_PIN      21
#define SCL_PIN      22
#define MPU_ADDR     0x68

// ---------------------------------------------------------------------------
// State & Variables
// ---------------------------------------------------------------------------
#define SONAR_TIMEOUT_US   30000UL
#define OBSTACLE_STOP_CM   31        
#define TELEMETRY_INTERVAL_MS 150

const int PWM_FREQ = 5000;
const int PWM_RES_BITS = 8;

enum RoverMode { MANUAL, ASSISTED, SEMI };
RoverMode currentMode = MANUAL;
String currentCommand = "S"; // F, B, L, R, S
int currentSpeedPct = 50;    // 0-100% from Dashboard
bool estopActive = false;

struct SonarReadings { float f, b, l, r; }; // mapped to front, back, left, right
SonarReadings tof = {400, 400, 400, 400};
float roverTilt = 0.0; // Computed from MPU

unsigned long lastTelemetry = 0;
unsigned long lastSensorRead = 0;

// ---------------------------------------------------------------------------
// Motor Control
// ---------------------------------------------------------------------------
void motorsInit() {
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT);
  
  // Note: Core 3.x API. Use ledcSetup/ledcAttachPin for Core 2.x
  ledcAttach(PWMA, PWM_FREQ, PWM_RES_BITS);
  ledcAttach(PWMB, PWM_FREQ, PWM_RES_BITS);
  
  digitalWrite(STBY, HIGH);
  ledcWrite(PWMA, 0); ledcWrite(PWMB, 0);
}

void setMotor(int pinIn1, int pinIn2, int pinPWM, int speed) {
  speed = constrain(speed, -255, 255);
  digitalWrite(pinIn1, speed >= 0 ? HIGH : LOW);
  digitalWrite(pinIn2, speed >= 0 ? LOW  : HIGH);
  ledcWrite(pinPWM, abs(speed));
}

void stopMotors() { setMotor(AIN1, AIN2, PWMA, 0); setMotor(BIN1, BIN2, PWMB, 0); }
void driveMotors(String dir, int speedPct) {
  if (estopActive || dir == "S") {
    stopMotors();
    return;
  }
  
  int pwm = map(speedPct, 0, 100, 0, 255);
  
  if (dir == "F") {
    if (currentMode != MANUAL && tof.f > 0 && tof.f < OBSTACLE_STOP_CM) { stopMotors(); return; }
    setMotor(AIN1, AIN2, PWMA, pwm); setMotor(BIN1, BIN2, PWMB, pwm);
  } else if (dir == "B") {
    if (currentMode != MANUAL && tof.b > 0 && tof.b < OBSTACLE_STOP_CM) { stopMotors(); return; }
    setMotor(AIN1, AIN2, PWMA, -pwm); setMotor(BIN1, BIN2, PWMB, -pwm);
  } else if (dir == "L") {
    setMotor(AIN1, AIN2, PWMA, -pwm); setMotor(BIN1, BIN2, PWMB, pwm);
  } else if (dir == "R") {
    setMotor(AIN1, AIN2, PWMA, pwm); setMotor(BIN1, BIN2, PWMB, -pwm);
  }
}

// ---------------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------------
void sonarsInit() {
  pinMode(FRONT_TRIG, OUTPUT); pinMode(REAR_TRIG, OUTPUT);
  pinMode(LEFT_TRIG, OUTPUT); pinMode(RIGHT_TRIG, OUTPUT);
  pinMode(FRONT_ECHO, INPUT); pinMode(REAR_ECHO, INPUT);
  pinMode(LEFT_ECHO, INPUT); pinMode(RIGHT_ECHO, INPUT);
}

float readSonarCm(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  unsigned long duration = pulseIn(echoPin, HIGH, SONAR_TIMEOUT_US);
  if (duration == 0) return 400.0;
  return (duration * 0.0343f) / 2.0f;
}

void mpuInit() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission();
}

void readSensors() {
  tof.f = readSonarCm(FRONT_TRIG, FRONT_ECHO); delay(5);
  tof.b = readSonarCm(REAR_TRIG, REAR_ECHO); delay(5);
  tof.l = readSonarCm(LEFT_TRIG, LEFT_ECHO); delay(5);
  tof.r = readSonarCm(RIGHT_TRIG, RIGHT_ECHO); delay(5);

  Wire.beginTransmission(MPU_ADDR); Wire.write(0x3B);
  if (Wire.endTransmission(false) == 0) {
    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6);
    if (Wire.available() == 6) {
      int16_t ax = (Wire.read() << 8) | Wire.read();
      int16_t ay = (Wire.read() << 8) | Wire.read();
      int16_t az = (Wire.read() << 8) | Wire.read();
      // Simple tilt calculation magnitude for dashboard
      roverTilt = abs(atan2(ay, az) * 180.0 / PI); 
    }
  }
}

// ---------------------------------------------------------------------------
// WebSocket Communication
// ---------------------------------------------------------------------------
void sendTelemetry() {
  StaticJsonDocument<256> doc;
  doc["t"] = "tele";
  doc["mode"] = (currentMode == SEMI) ? "SEMI" : (currentMode == ASSISTED) ? "ASSISTED" : "MANUAL";
  doc["speed"] = currentSpeedPct;
  doc["estop"] = estopActive ? 1 : 0;
  doc["rssi"] = WiFi.RSSI();
  doc["tilt"] = roverTilt;
  
  JsonObject tofJson = doc.createNestedObject("tof");
  tofJson["f"] = tof.f; tofJson["b"] = tof.b;
  tofJson["l"] = tof.l; tofJson["r"] = tof.r;

  String output;
  serializeJson(doc, output);
  webSocket.broadcastTXT(output);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload)) return;

    if (doc.containsKey("cmd")) {
      String cmd = doc["cmd"].as<String>();
      
      if (cmd == "drive") {
        if (doc.containsKey("dir")) currentCommand = doc["dir"].as<String>();
        if (doc.containsKey("speed")) currentSpeedPct = doc["speed"].as<int>();
      } 
      else if (cmd == "speed") {
        currentSpeedPct = doc["val"].as<int>();
      } 
      else if (cmd == "mode") {
        String m = doc["val"].as<String>();
        if (m == "MANUAL") currentMode = MANUAL;
        else if (m == "ASSISTED") currentMode = ASSISTED;
        else if (m == "SEMI") currentMode = SEMI;
      } 
      else if (cmd == "estop") {
        estopActive = true;
      } 
      else if (cmd == "estop_reset") {
        estopActive = false;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Main Setup & Loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(ESTOP_PIN, INPUT_PULLDOWN);

  motorsInit();
  sonarsInit();
  mpuInit();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  Serial.println("\n--- WiFi Connected ---");
  Serial.println("Dashboard Connection Configuration:");
  Serial.print("WEBSOCKET URL: ws://");
  Serial.print(WiFi.localIP());
  Serial.println(":81/");
  Serial.println("-----------------------------------");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  unsigned long now = millis();
  webSocket.loop();

  // Hardware Emergency Stop overrides software
  if (digitalRead(ESTOP_PIN) == HIGH) estopActive = true; 

  driveMotors(currentCommand, currentSpeedPct);

  if (now - lastSensorRead >= 50) {
    lastSensorRead = now;
    readSensors();
  }

  if (now - lastTelemetry >= TELEMETRY_INTERVAL_MS) {
    lastTelemetry = now;
    sendTelemetry();
  }
}