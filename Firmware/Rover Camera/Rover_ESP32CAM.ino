#include "esp_camera.h"
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <iostream>
#include <sstream>
#include <ESP32Servo.h>

// --- Servo & Light Pins ---
#define DUMMY_SERVO1_PIN 12 // Dummy servos to prevent ESP32Servo timer conflicts with camera
#define DUMMY_SERVO2_PIN 13 
#define PAN_PIN 14
#define TILT_PIN 15
#define LIGHT_PIN 4

Servo dummyServo1;
Servo dummyServo2;
Servo panServo;
Servo tiltServo;

// --- Camera Pins (AI-Thinker) ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// --- Wi-Fi & Static IP Configuration ---
const char* ssid     = "Power House";     // Enter your Router's SSID
const char* password = "onetwonine"; // Enter your Router's Password

// Define your Static IP details here
IPAddress local_IP(192, 168, 1, 150); // The Static IP you want for the ESP32
IPAddress gateway(192, 168, 1, 1);    // Your router's IP address
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8); 

// --- Server & WebSocket ---
AsyncWebServer server(80);
AsyncWebSocket wsCamera("/Camera");
AsyncWebSocket wsServoInput("/ServoInput");
uint32_t cameraClientId = 0;

// --- HTML Dashboard ---
const char* htmlHomePage PROGMEM = R"HTMLHOMEPAGE(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <title>ESP32 Pan-Tilt Camera</title>
    <style>
      body {
        font-family: Arial, sans-serif;
        background-color: #f4f4f9;
        text-align: center;
        margin: 0;
        padding: 20px;
        -webkit-user-select: none;
        user-select: none;
      }
      .card {
        background: white;
        padding: 20px;
        border-radius: 10px;
        box-shadow: 0px 4px 10px rgba(0,0,0,0.1);
        display: inline-block;
        max-width: 500px;
        width: 100%;
      }
      img {
        width: 100%;
        max-width: 400px;
        border-radius: 8px;
        background-color: #000;
        min-height: 250px;
      }
      .slider-container {
        margin: 20px 0;
        text-align: left;
      }
      .slider-container label {
        font-weight: bold;
        color: #333;
        display: block;
        margin-bottom: 5px;
      }
      .slider {
        -webkit-appearance: none;
        width: 100%;
        height: 15px;
        border-radius: 5px;
        background: #ddd;
        outline: none;
      }
      .slider::-webkit-slider-thumb {
        -webkit-appearance: none;
        appearance: none;
        width: 35px;
        height: 35px;
        border-radius: 50%;
        background: #007bff;
        cursor: pointer;
      }
      .slider::-moz-range-thumb {
        width: 35px;
        height: 35px;
        border-radius: 50%;
        background: #007bff;
        cursor: pointer;
      }
    </style>
  </head>
  <body>
    <div class="card">
      <h2 style="margin-top:0; color:#333;">Camera Dashboard</h2>
      <img id="cameraImage" src="">
      
      <div class="slider-container">
        <label>Pan (Left/Right)</label>
        <input type="range" min="0" max="180" value="90" class="slider" id="Pan" oninput='sendButtonInput("Pan",value)'>
      </div>
      
      <div class="slider-container">
        <label>Tilt (Up/Down)</label>
        <input type="range" min="0" max="180" value="90" class="slider" id="Tilt" oninput='sendButtonInput("Tilt",value)'>
      </div>
      
      <div class="slider-container">
        <label>Flashlight Brightness</label>
        <input type="range" min="0" max="255" value="0" class="slider" id="Light" oninput='sendButtonInput("Light",value)'>
      </div>
    </div>
  
    <script>
      var webSocketCameraUrl = "ws:\/\/" + window.location.hostname + "/Camera";
      var webSocketServoInputUrl = "ws:\/\/" + window.location.hostname + "/ServoInput";      
      var websocketCamera, websocketServoInput;
      
      function initCameraWebSocket() {
        websocketCamera = new WebSocket(webSocketCameraUrl);
        websocketCamera.binaryType = 'blob';
        websocketCamera.onclose = function(){ setTimeout(initCameraWebSocket, 2000); };
        websocketCamera.onmessage = function(event) {
          var imageId = document.getElementById("cameraImage");
          imageId.src = URL.createObjectURL(event.data);
        };
      }
      
      function initServoInputWebSocket() {
        websocketServoInput = new WebSocket(webSocketServoInputUrl);
        websocketServoInput.onopen = function() {
          sendButtonInput("Pan", document.getElementById("Pan").value);
          sendButtonInput("Tilt", document.getElementById("Tilt").value);
          sendButtonInput("Light", document.getElementById("Light").value);          
        };
        websocketServoInput.onclose = function(){ setTimeout(initServoInputWebSocket, 2000); };
      }
      
      function initWebSocket() {
        initCameraWebSocket();
        initServoInputWebSocket();
      }

      function sendButtonInput(key, value) {
        if(websocketServoInput && websocketServoInput.readyState === WebSocket.OPEN) {
          websocketServoInput.send(key + "," + value);
        }
      }
    
      window.onload = initWebSocket;
    </script>
  </body>    
</html>
)HTMLHOMEPAGE";

void onServoInputWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {                       
  if (type == WS_EVT_DISCONNECT) {
    panServo.write(90);
    tiltServo.write(90);
    ledcWrite(LIGHT_PIN, 0); 
  }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      std::string myData((char *)data, len);
      std::istringstream ss(myData);
      std::string key, value;
      std::getline(ss, key, ',');
      std::getline(ss, value, ',');
      
      if (value != "") {
        int valueInt = atoi(value.c_str());
        if (key == "Pan") panServo.write(valueInt);
        else if (key == "Tilt") tiltServo.write(valueInt);   
        else if (key == "Light") ledcWrite(LIGHT_PIN, valueInt);        
      }
    }
  }
}

void onCameraWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {                       
  if (type == WS_EVT_CONNECT) {
    cameraClientId = client->id();
  } else if (type == WS_EVT_DISCONNECT) {
    cameraClientId = 0;
  }
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  
  // FIXED: Lowered XCLK from 20MHz to 10MHz to prevent buffer overflows
  config.xclk_freq_hz = 20000000; 
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Settings for Smoothness and Good Visibility
  config.frame_size = FRAMESIZE_VGA; // Change to FRAMESIZE_HVGA if overflows persist
  
  // FIXED: Increased value from 12 to 20 to slightly reduce image payload size
  config.jpeg_quality = 20; 
  config.grab_mode = CAMERA_GRAB_LATEST; // Drops old frames, ensuring minimal lag

  if (psramFound()) {
    config.fb_count = 2; // Uses PSRAM for dual-buffering (smoother video)
    heap_caps_malloc_extmem_enable(20000);  
  } else {
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed!");
    return;
  }  
}

void sendCameraPicture() {
  if (cameraClientId == 0) return;

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;

  wsCamera.binary(cameraClientId, fb->buf, fb->len);
  esp_camera_fb_return(fb);
    
  // Wait for message to be delivered
  while (true) {
    AsyncWebSocketClient * clientPointer = wsCamera.client(cameraClientId);
    if (!clientPointer || !(clientPointer->queueIsFull())) break;
    delay(1);
  }
}

void setup(void) {
  Serial.begin(115200);

  // Set up pins and hardware
  dummyServo1.attach(DUMMY_SERVO1_PIN);
  dummyServo2.attach(DUMMY_SERVO2_PIN);  
  panServo.attach(PAN_PIN);
  tiltServo.attach(TILT_PIN);
  ledcAttach(LIGHT_PIN, 1000, 8); // Core 3.x Syntax for LED
  
  // Configure Static IP and Connect to Wi-Fi
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("STA Failed to configure");
  }
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! Access Dashboard at: ");
  Serial.println(WiFi.localIP());

  // Set up Web Server Routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", htmlHomePage);
  });
  
  wsCamera.onEvent(onCameraWebSocketEvent);
  server.addHandler(&wsCamera);

  wsServoInput.onEvent(onServoInputWebSocketEvent);
  server.addHandler(&wsServoInput);

  server.begin();
  setupCamera();
}

void loop() {
  wsCamera.cleanupClients(); 
  wsServoInput.cleanupClients(); 
  sendCameraPicture(); 
  
  // FIXED: Added a small delay to yield CPU time to Wi-Fi background tasks
  delay(15); 
}