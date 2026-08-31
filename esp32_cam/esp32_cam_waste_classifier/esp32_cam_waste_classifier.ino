/**
 * Smart Waste Segregation System - ESP32-CAM Production Firmware
 *
 * Hardware Configuration:
 *   - Board: AI-Thinker ESP32-CAM (OV2640 2MP Camera Sensor)
 *   - Ultrasonic Sensor: HC-SR04
 *       TRIG -> GPIO 13
 *       ECHO -> GPIO 12
 *   - Flash LED: Onboard High-Power White LED -> GPIO 4
 *   - NO SERVO MOTOR (Servo handled separately by DevKit/Controller)
 *
 * Operational Flow:
 *   1. Connects to Wi-Fi with Public DNS (8.8.8.8 / 1.1.1.1) to guarantee DNS resolution.
 *   2. Starts local camera web server (/, /stream, /capture).
 *   3. Ultrasonic sensor measures distance continuously.
 *   4. When object distance < 15 cm (and >= 2 cm):
 *      - Waits 400ms for waste object to settle.
 *      - Turns Flash LED ON (GPIO 4 HIGH).
 *      - Captures fresh JPEG image from OV2640.
 *      - Transmits frame to AI Backend:
 *          * Priority 1: Fast Direct HTTP (http://<Gateway-IP>:5000/predict) if on same LAN.
 *          * Priority 2: Cloud HTTPS (https://garbage-segregation.pages.dev/predict).
 *      - Backend stores image in Firebase Storage:
 *        gs://garbage-fa1b3.firebasestorage.app/captures/
 *      - Receives prediction result (Class, Confidence, Servo Angle, Image URL).
 *      - Turns Flash LED OFF (GPIO 4 LOW).
 *      - 3-second cooldown before accepting next object.
 *   5. Sends Firebase Heartbeat every 10 seconds to /hardware_heartbeats/esp32_cam.json.
 *
 * Local Camera Endpoints:
 *   http://<ESP32-IP>/
 *   http://<ESP32-IP>/stream
 *   http://<ESP32-IP>/capture
 */

// ==========================================
// Libraries
// ==========================================

#include "esp_camera.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ==========================================
// Wi-Fi Configuration
// ==========================================

const char *WIFI_SSID     = "aniket";
const char *WIFI_PASSWORD = "12345678";

// ==========================================
// Cloud Backend Configuration
// ==========================================

const char *SERVER_HOST   = "garbage-backend-hq2k3sj6ra-ew.a.run.app";
const int   SERVER_PORT   = 443;
const char *SERVER_PATH   = "/predict";

// Cloudflare Anycast fallback IP if local hotspot DNS drops
const char *SERVER_FALLBACK_IP = "172.66.47.139";

// Local Flask Backend Port on LAN Gateway (Optional fast bypass)
const int   LOCAL_PORT    = 5000;

// ==========================================
// AI-Thinker ESP32-CAM Pin Definitions
// ==========================================

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

// ==========================================
// Hardware Pins
// ==========================================

#define FLASH_LED_PIN      4
#define SENSOR_TRIG_PIN   13
#define SENSOR_ECHO_PIN   12

// ==========================================
// Detection Settings
// ==========================================

#define DETECTION_THRESHOLD_CM 15.0

// ==========================================
// Web Server & State
// ==========================================

WebServer server(80);

bool isProcessing = false;
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL = 10000;

// ==========================================
// Function Declarations
// ==========================================

void setupCamera();
float measureDistanceCM();
void detectAndSendToServer();
void sendHeartbeat();
void handleRoot();
void handleStream();
void handleCapture();

// ==========================================
// SETUP
// ==========================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" SMART WASTE SEGREGATION - ESP32-CAM");
  Serial.println("=========================================");

  // 1. Configure Flash LED
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  // 2. Configure HC-SR04 Ultrasonic Sensor
  pinMode(SENSOR_TRIG_PIN, OUTPUT);
  pinMode(SENSOR_ECHO_PIN, INPUT);
  digitalWrite(SENSOR_TRIG_PIN, LOW);

  // 3. Initialize Camera
  setupCamera();

  // 4. Configure Public DNS (Google DNS 8.8.8.8, Cloudflare 1.1.1.1) to avoid hotspot drops
  WiFi.mode(WIFI_STA);
  IPAddress dns1(8, 8, 8, 8);
  IPAddress dns2(1, 1, 1, 1);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns1, dns2);

  // 5. Connect to Wi-Fi
  Serial.printf("[*] Connecting to Wi-Fi '%s'", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("[+] Wi-Fi Connected Successfully!");
  Serial.printf("[+] ESP32-CAM Local IP Address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[+] Network Gateway IP        : %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("[+] Active DNS Server         : %s\n", WiFi.dnsIP().toString().c_str());

  // 6. Start Local Camera Web Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/capture", HTTP_GET, handleCapture);
  server.begin();

  Serial.println();
  Serial.println("=========================================");
  Serial.println("      CAMERA WEB SERVER INITIALIZED      ");
  Serial.println("=========================================");
  Serial.printf("  Camera Preview Page : http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Live MJPEG Stream   : http://%s/stream\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Single Frame Capture: http://%s/capture\n", WiFi.localIP().toString().c_str());
  Serial.println("=========================================");
  Serial.println("[+] System Ready - Monitoring for Waste Objects...\n");
}

// ==========================================
// LOOP
// ==========================================

void loop() {
  // 1. Handle incoming HTTP requests for live streaming & capture
  server.handleClient();

  // 2. Periodic Firebase Heartbeat (every 10s)
  if (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
    lastHeartbeatTime = millis();
    sendHeartbeat();
  }

  // 3. Skip sensor trigger if already processing
  if (isProcessing) {
    delay(10);
    return;
  }

  // 4. Measure distance with ultrasonic sensor
  float distance = measureDistanceCM();

  if (distance > 0 && distance < 50.0) {
    Serial.printf("[SENSOR] Distance: %.2f cm\n", distance);
  }

  // 5. Object Detection Trigger (< 15 cm and valid reading >= 2 cm)
  if (distance >= 2.0 && distance < DETECTION_THRESHOLD_CM) {
    Serial.println();
    Serial.println("*****************************************");
    Serial.printf("[!] OBJECT DETECTED! Distance: %.2f cm\n", distance);
    Serial.println("*****************************************");

    isProcessing = true;

    // Allow waste object to settle in bin chute
    delay(400);

    // Turn Flash LED ON
    Serial.println("[*] Flash LED ON");
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(300);

    // Capture and upload frame
    detectAndSendToServer();

    // Turn Flash LED OFF
    digitalWrite(FLASH_LED_PIN, LOW);
    Serial.println("[*] Flash LED OFF");

    // Cooldown pause
    Serial.println("[*] Cooldown: 3 seconds...");
    delay(3000);

    isProcessing = false;
    Serial.println("\n[+] Ready for next object.\n");
  }

  delay(150);
}

// ==========================================
// CAMERA INITIALIZATION
// ==========================================

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count     = 2;
    config.grab_mode    = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size   = FRAMESIZE_CIF;
    config.jpeg_quality = 12;
    config.fb_count     = 1;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[!] Camera init failed (0x%x). Retrying at 10MHz...\n", err);
    config.xclk_freq_hz = 10000000;
    err = esp_camera_init(&config);
  }

  if (err != ESP_OK) {
    Serial.printf("[!] Fatal: Camera init failed: 0x%x\n", err);
    while (true) { delay(1000); }
  }

  Serial.println("[+] OV2640 Camera initialized successfully.");
}

// ==========================================
// ULTRASONIC DISTANCE MEASUREMENT
// ==========================================

float measureDistanceCM() {
  digitalWrite(SENSOR_TRIG_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(SENSOR_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(SENSOR_TRIG_PIN, LOW);

  unsigned long duration = pulseIn(SENSOR_ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1.0;

  float distance = (duration * 0.0343) / 2.0;
  if (distance < 2.0 || distance > 400.0) return -1.0;

  return distance;
}

// ==========================================
// CAPTURE + SEND IMAGE TO SERVER
// ==========================================

void detectAndSendToServer() {
  Serial.println("[*] Capturing image...");

  // 1. Flush stale DMA buffer
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
  }

  // 2. Grab fresh frame
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[!] Camera capture failed!");
    return;
  }

  Serial.printf("[+] Image captured: %u bytes\n", fb->len);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[!] Wi-Fi disconnected!");
    esp_camera_fb_return(fb);
    return;
  }

  Serial.println("[*] Wi-Fi OK");
  Serial.printf("[*] ESP32 IP: %s | Gateway IP: %s\n", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());

  String responseBody = "";
  bool uploadSuccess = false;

  // ==========================================
  // Priority 1: Fast Direct HTTP (Local Gateway LAN)
  // ==========================================
  WiFiClient localClient;
  localClient.setTimeout(3);

  if (localClient.connect(WiFi.gatewayIP(), LOCAL_PORT)) {
    Serial.printf("[+] Connected to Local Backend at http://%s:%d/predict\n", WiFi.gatewayIP().toString().c_str(), LOCAL_PORT);
    localClient.printf("POST /predict HTTP/1.1\r\n");
    localClient.printf("Host: %s:%d\r\n", WiFi.gatewayIP().toString().c_str(), LOCAL_PORT);
    localClient.print("Content-Type: image/jpeg\r\n");
    localClient.printf("Content-Length: %u\r\n", fb->len);
    localClient.print("User-Agent: ESP32-CAM-Waste-Detector\r\n");
    localClient.print("Connection: close\r\n\r\n");

    localClient.write(fb->buf, fb->len);
    Serial.println("[*] Image sent. Waiting for response...");

    unsigned long timeout = millis();
    bool isBody = false;

    while (localClient.connected() && millis() - timeout < 8000) {
      while (localClient.available()) {
        String line = localClient.readStringUntil('\n');
        line.trim();
        if (!isBody && line.length() == 0) {
          isBody = true;
          continue;
        }
        if (isBody) {
          responseBody += line;
        }
        timeout = millis();
      }
    }
    localClient.stop();
    uploadSuccess = (responseBody.length() > 0);
  }

  // ==========================================
  // Priority 2: Cloud HTTPS (https://garbage-segregation.pages.dev/predict)
  // ==========================================
  if (!uploadSuccess) {
    Serial.printf("[*] Connecting to Cloud HTTPS: https://%s%s ...\n", SERVER_HOST, SERVER_PATH);

    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    secureClient.setTimeout(15);
    secureClient.setHandshakeTimeout(15);

    bool connected = secureClient.connect(SERVER_HOST, SERVER_PORT);
    if (!connected) {
      // Retry with Anycast fallback IP
      Serial.printf("[!] Hostname connect failed. Retrying with Anycast IP (%s)...\n", SERVER_FALLBACK_IP);
      IPAddress cloudIP;
      cloudIP.fromString(SERVER_FALLBACK_IP);
      connected = secureClient.connect(cloudIP, SERVER_PORT);
    }

    if (connected) {
      Serial.println("[+] Connected to Cloud HTTPS!");
      secureClient.printf("POST %s HTTP/1.1\r\n", SERVER_PATH);
      secureClient.printf("Host: %s\r\n", SERVER_HOST);
      secureClient.print("Content-Type: image/jpeg\r\n");
      secureClient.printf("Content-Length: %u\r\n", fb->len);
      secureClient.print("User-Agent: ESP32-CAM-Waste-Detector\r\n");
      secureClient.print("Connection: close\r\n\r\n");

      secureClient.write(fb->buf, fb->len);
      Serial.println("[*] Image sent to Cloud. Waiting for response...");

      unsigned long timeout = millis();
      bool isBody = false;

      while (secureClient.connected() && millis() - timeout < 12000) {
        while (secureClient.available()) {
          String line = secureClient.readStringUntil('\n');
          line.trim();
          if (!isBody && line.length() == 0) {
            isBody = true;
            continue;
          }
          if (isBody) {
            responseBody += line;
          }
          timeout = millis();
        }
      }
      secureClient.stop();
      uploadSuccess = (responseBody.length() > 0);
    } else {
      Serial.println("[!] Cloud HTTPS connection failed.");
    }
  }

  esp_camera_fb_return(fb);
  Serial.println("[+] Connection closed");

  // ==========================================
  // Parse Classification JSON & Firebase Storage URL
  // ==========================================
  if (responseBody.length() > 0) {
    int jsonStart = responseBody.indexOf('{');
    int jsonEnd   = responseBody.lastIndexOf('}');
    if (jsonStart != -1 && jsonEnd != -1 && jsonEnd >= jsonStart) {
      String jsonStr = responseBody.substring(jsonStart, jsonEnd + 1);
      StaticJsonDocument<768> doc;
      DeserializationError error = deserializeJson(doc, jsonStr);
      if (!error) {
        const char *wasteClass = doc["prediction"]["class"] | "Unknown";
        float confidence = doc["prediction"]["confidence"] | 0.0;
        int servoAngle = doc["prediction"]["servo_angle"] | 0;
        const char *imageUrl = doc["image_url"] | doc["prediction"]["image_url"] | "";

        Serial.println();
        Serial.println("=========================================");
        Serial.printf("  [RESULT] Classification : %s\n", wasteClass);
        Serial.printf("  [RESULT] Confidence     : %.1f%%\n", confidence * 100.0);
        Serial.printf("  [RESULT] Servo Angle    : %d° (%s)\n", servoAngle, (servoAngle == 180 ? "WET BIN" : "DRY BIN"));
        if (strlen(imageUrl) > 0) {
          Serial.printf("  [RESULT] Firebase Image : %s\n", imageUrl);
        }
        Serial.println("=========================================\n");
      }
    }
  }
}

// ==========================================
// FIREBASE REALTIME DATABASE HEARTBEAT
// ==========================================

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8);

  if (!client.connect("garbage-fa1b3-default-rtdb.firebaseio.com", 443)) {
    return;
  }

  String payload = "{\"device\":\"esp32_cam\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"status\":\"ONLINE\",\"timestamp\":{\".sv\":\"timestamp\"}}";

  client.println("PUT /hardware_heartbeats/esp32_cam.json HTTP/1.1");
  client.println("Host: garbage-fa1b3-default-rtdb.firebaseio.com");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(payload.length());
  client.println("Connection: close");
  client.println();
  client.print(payload);

  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 2000) break;
    delay(10);
  }

  client.stop();
}

// ==========================================
// LOCAL CAMERA WEB PAGE
// ==========================================

void handleRoot() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-CAM Live Camera</title>
  <style>
    body { margin: 0; padding: 20px; background: #111; color: white; font-family: Arial; text-align: center; }
    h1 { margin-bottom: 20px; }
    img { width: 100%; max-width: 800px; height: auto; border: 2px solid #444; border-radius: 10px; }
    button { margin-top: 20px; padding: 12px 25px; font-size: 16px; border: none; border-radius: 6px; cursor: pointer; }
  </style>
</head>
<body>
  <h1>ESP32-CAM Live Camera</h1>
  <img src="/stream">
  <br>
  <button onclick="location.href='/capture'">Capture Image</button>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", page);
}

// ==========================================
// LIVE CAMERA STREAM
// ==========================================

void handleStream() {
  WiFiClient client = server.client();

  client.print("HTTP/1.1 200 OK\r\n"
               "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
               "Cache-Control: no-cache\r\n"
               "Pragma: no-cache\r\n"
               "Access-Control-Allow-Origin: *\r\n\r\n");

  Serial.println("[STREAM] Client connected");

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[STREAM] Camera capture failed");
      break;
    }

    client.printf("--frame\r\n"
                  "Content-Type: image/jpeg\r\n"
                  "Content-Length: %u\r\n\r\n",
                  fb->len);

    client.write(fb->buf, fb->len);
    client.print("\r\n");

    esp_camera_fb_return(fb);
    delay(50);
  }

  Serial.println("[STREAM] Client disconnected");
}

// ==========================================
// SINGLE IMAGE CAPTURE
// ==========================================

void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg");

  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);

  esp_camera_fb_return(fb);
}
