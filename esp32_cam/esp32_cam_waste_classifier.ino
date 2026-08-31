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
 *   1. Connects to Wi-Fi with Public DNS (8.8.8.8 / 1.1.1.1).
 *   2. Starts local camera web server (/, /stream, /capture).
 *   3. Ultrasonic sensor measures distance continuously.
 *   4. When object distance < 15 cm (and >= 2 cm):
 *      - Waits 400ms for waste object to settle.
 *      - Turns Flash LED ON (GPIO 4 HIGH).
 *      - Captures fresh JPEG image from OV2640.
 *      - Transmits frame to AI Backend:
 *          * Priority 1: Fast Direct Local HTTP (http://<Gateway-IP>:5000/predict).
 *          * Priority 2: Google Cloud Run (https://garbage-backend-hq2k3sj6ra-ew.a.run.app/predict).
 *          * Priority 3: Cloudflare Edge (https://garbage-segregation.pages.dev/predict).
 *      - Backend stores image in Firebase Storage:
 *        gs://garbage-fa1b3.firebasestorage.app/captures/
 *      - Receives prediction result (Class, Confidence, Servo Angle, Image URL).
 *      - Turns Flash LED OFF (GPIO 4 LOW).
 *      - 3-second cooldown before accepting next object.
 *   5. Sends Firebase Heartbeat every 10 seconds to /hardware_heartbeats/esp32_cam.json.
 */

#include "esp_camera.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

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
// Peripheral Pins & Settings
// ==========================================

#define FLASH_LED_PIN      4
#define SENSOR_TRIG_PIN   13
#define SENSOR_ECHO_PIN   12

const float DETECTION_THRESHOLD_CM = 15.0;
const unsigned long HEARTBEAT_INTERVAL = 10000;

// ==========================================
// Global State
// ==========================================

WebServer server(80);
unsigned long lastHeartbeatTime = 0;
bool isProcessing = false;

// Forward declarations
void handleRoot();
void handleStream();
void handleCapture();
void sendHeartbeat();
void detectAndSendToServer();
float measureDistanceCM();
void setupCamera();

// ==========================================
// HTTP HANDLERS FOR LOCAL CAMERA WEB SERVER
// ==========================================

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>ESP32-CAM Live View</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial,sans-serif;background:#111;color:#fff;text-align:center;padding:20px;}";
  html += "img{max-width:100%;height:auto;border:2px solid #34d399;border-radius:8px;}";
  html += ".btn{display:inline-block;padding:10px 20px;margin:10px;background:#10b981;color:#fff;text-decoration:none;border-radius:6px;}";
  html += "</style></head><body>";
  html += "<h2>🗑️ Smart Waste Classifier - ESP32-CAM</h2>";
  html += "<p>Live Camera Stream:</p>";
  html += "<img src='/stream' /><br>";
  html += "<a href='/capture' class='btn' target='_blank'>Capture Single Frame</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleStream() {
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) break;

    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n";
    response += "Content-Length: " + String(fb->len) + "\r\n\r\n";
    server.sendContent(response);

    client.write(fb->buf, fb->len);
    server.sendContent("\r\n");
    esp_camera_fb_return(fb);
    delay(30);
  }
}

void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

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

  // 4. Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  Serial.printf("[*] Connecting to Wi-Fi '%s'", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Override Windows Hotspot DNS with Google Public DNS (8.8.8.8) and Cloudflare (1.1.1.1)
  IPAddress dns1(8, 8, 8, 8);
  IPAddress dns2(1, 1, 1, 1);
  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);

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
  server.handleClient();

  if (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
    lastHeartbeatTime = millis();
    sendHeartbeat();
  }

  if (isProcessing) {
    delay(10);
    return;
  }

  float distance = measureDistanceCM();

  if (distance > 0 && distance < 50.0) {
    Serial.printf("[SENSOR] Distance: %.2f cm\n", distance);
  }

  if (distance >= 2.0 && distance < DETECTION_THRESHOLD_CM) {
    Serial.println();
    Serial.println("*****************************************");
    Serial.printf("[!] OBJECT DETECTED! Distance: %.2f cm\n", distance);
    Serial.println("*****************************************");

    isProcessing = true;
    delay(400);

    Serial.println("[*] Flash LED ON");
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(300);

    detectAndSendToServer();

    digitalWrite(FLASH_LED_PIN, LOW);
    Serial.println("[*] Flash LED OFF");

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

  camera_fb_t *fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
  }

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
  // Priority 1: Fast Direct Local HTTP (<30ms on LAN Gateway)
  // ==========================================
  HTTPClient http;
  String localUrl = "http://" + WiFi.gatewayIP().toString() + ":" + String(LOCAL_PORT) + "/predict";
  http.begin(localUrl);
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("User-Agent", "ESP32-CAM-Waste-Detector");
  http.setTimeout(1500);

  int httpCode = http.POST(fb->buf, fb->len);
  if (httpCode == 200) {
    Serial.printf("[+] Local Backend Connected (HTTP %d)\n", httpCode);
    responseBody = http.getString();
    uploadSuccess = true;
  }
  http.end();

  // ==========================================
  // Priority 2: Google Cloud Run AI Backend (HTTPS)
  // ==========================================
  if (!uploadSuccess) {
    Serial.println("[*] Connecting to Google Cloud Run: https://garbage-backend-hq2k3sj6ra-ew.a.run.app/predict ...");
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient https;
    https.begin(secureClient, "https://garbage-backend-hq2k3sj6ra-ew.a.run.app/predict");
    https.addHeader("Content-Type", "image/jpeg");
    https.addHeader("User-Agent", "ESP32-CAM-Waste-Detector");
    https.setTimeout(15000);

    int cloudCode = https.POST(fb->buf, fb->len);
    if (cloudCode == 200) {
      Serial.printf("[+] Cloud Run Connected (HTTP %d)\n", cloudCode);
      responseBody = https.getString();
      uploadSuccess = true;
    } else {
      Serial.printf("[!] Cloud Run POST error (%d): %s\n", cloudCode, https.errorToString(cloudCode).c_str());
    }
    https.end();
  }

  // ==========================================
  // Priority 3: Cloudflare Pages Edge Fallback (HTTPS)
  // ==========================================
  if (!uploadSuccess) {
    Serial.println("[*] Connecting to Cloudflare Edge: https://garbage-segregation.pages.dev/predict ...");
    WiFiClientSecure edgeClient;
    edgeClient.setInsecure();

    HTTPClient httpsEdge;
    httpsEdge.begin(edgeClient, "https://garbage-segregation.pages.dev/predict");
    httpsEdge.addHeader("Content-Type", "image/jpeg");
    httpsEdge.addHeader("User-Agent", "ESP32-CAM-Waste-Detector");
    httpsEdge.setTimeout(15000);

    int edgeCode = httpsEdge.POST(fb->buf, fb->len);
    if (edgeCode == 200) {
      Serial.printf("[+] Cloudflare Edge Connected (HTTP %d)\n", edgeCode);
      responseBody = httpsEdge.getString();
      uploadSuccess = true;
    } else {
      Serial.printf("[!] Cloudflare Edge error (%d): %s\n", edgeCode, httpsEdge.errorToString(edgeCode).c_str());
    }
    httpsEdge.end();
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
  client.setTimeout(4);

  const char *host = "garbage-fa1b3-default-rtdb.firebaseio.com";
  if (!client.connect(host, 443)) return;

  String payload = "{\"device\":\"esp32_cam\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"status\":\"ONLINE\",\"rssi\":" + String(WiFi.RSSI()) + ",\"timestamp\":{\".sv\":\"timestamp\"}}";

  client.println("PUT /hardware_heartbeats/esp32_cam.json HTTP/1.1");
  client.printf("Host: %s\r\n", host);
  client.println("Content-Type: application/json");
  client.printf("Content-Length: %d\r\n", payload.length());
  client.println("Connection: close\r\n");
  client.println(payload);

  client.stop();
}
