/**
 * ============================================================================
 * ESP32-CAM Machine Learning Garbage Classification Client Firmware
 * ============================================================================
 * 
 * Hardware Platform:
 * - Module: AI-Thinker ESP32-CAM with OV2640 Camera
 * - Power: 5V DC (Minimum 2A supply recommended)
 * - Interfaces:
 *     - Flash LED -> GPIO 4
 *     - Object Trigger / Push Button / Proximity Sensor -> GPIO 13 (Active LOW)
 *     - UART TX -> GPIO 1 (Connects to Arduino UNO SoftwareSerial D7)
 *     - UART RX -> GPIO 3 (Connects to Arduino UNO SoftwareSerial D8)
 * 
 * Software Dependencies (Arduino IDE / PlatformIO):
 * - Board Package: ESP32 by Espressif Systems (Version 2.x or 3.x)
 * - Board Selection: "AI Thinker ESP32-CAM"
 * - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 * - PSRAM: "Enabled"
 * - Required Libraries:
 *     - WiFi (Built-in ESP32)
 *     - HTTPClient (Built-in ESP32)
 *     - esp_camera.h (Built-in ESP32 Camera Driver)
 *     - ArduinoJson (Version 6.x or 7.x by Benoit Blanchon)
 * ============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <ArduinoJson.h>

// ============================================================================
// 1. NETWORK & API CONFIGURATION
// ============================================================================
// Update Wi-Fi credentials to match your local network access point
const char* WIFI_SSID     = "aniket";
const char* WIFI_PASSWORD = "12345678";

// Server URL pointing to your Python ML REST API host (Port 8000)
const char* SERVER_URL    = "http://192.168.86.189:8000/predict";

// Machine Learning Confidence Validation Guardrail
// Predictions below this confidence threshold will be flagged as "UNKNOWN"
const float CONFIDENCE_THRESHOLD = 0.70; // 70% confidence threshold (0.0 to 1.0)

// Optional Static IP Configuration
const bool USE_STATIC_IP  = false; 
IPAddress local_IP(192, 168, 86, 200);  // Fixed IP for ESP32-CAM
IPAddress gateway(192, 168, 86, 1);     // Gateway IP
IPAddress subnet(255, 255, 255, 0);     // Subnet Mask
IPAddress primaryDNS(8, 8, 8, 8);       // Google DNS

// ============================================================================
// 2. HARDWARE PIN DEFINITIONS (AI-THINKER BOARD)
// ============================================================================
#define FLASH_LED_PIN   4    // Onboard High-Brightness Flash LED (GPIO 4)
#define TRIGGER_PIN     13   // Hardware Object Trigger / Button (Active LOW)

// Select AI-Thinker Camera Pinout Model
#define CAMERA_MODEL_AI_THINKER
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

// System Timers & Cooldown
unsigned long lastCaptureTime = 0;
const unsigned long CAPTURE_DEBOUNCE_MS = 3000; // 3 seconds cooldown between captures

// ============================================================================
// 3. WI-FI RECONNECTION & HEALTH CHECK
// ============================================================================
void verifyWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WI-FI] Connection lost! Attempting reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
      delay(500);
      Serial.print(".");
      retries++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WI-FI] Reconnected! Device IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("\n[ERROR] Wi-Fi reconnect failed!");
    }
  }
}

// ============================================================================
// 4. IMAGE CAPTURE & HTTP POST TO ML API
// ============================================================================

/**
 * Captures an image frame using OV2640 camera, formats payload as HTTP multipart/form-data,
 * POSTs payload to ML API server, parses returned classification JSON, validates confidence threshold,
 * and sends resulting command string to Arduino UNO over UART Serial (GPIO 1 TX).
 */
bool captureAndClassifyWaste() {
  verifyWiFiConnection();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ERROR] Cannot send image: No Wi-Fi connection!");
    Serial.println("UNKNOWN"); // Send UNKNOWN to Arduino
    return false;
  }

  Serial.println("\n=================================================");
  Serial.println("[CAMERA] Capturing garbage image frame...");

  // Turn on onboard flash LED briefly for illumination
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(150);

  // Capture frame buffer from OV2640 sensor
  camera_fb_t * fb = esp_camera_fb_get();
  digitalWrite(FLASH_LED_PIN, LOW); // Turn off flash LED

  if (!fb) {
    Serial.println("[FATAL ERROR] Camera frame buffer capture failed!");
    Serial.println("UNKNOWN");
    return false;
  }

  Serial.printf("[INFO] Frame captured successfully! Size: %u bytes\n", fb->len);

  // Initialize HTTP Client
  HTTPClient http;
  http.begin(SERVER_URL);
  http.setTimeout(12000); // 12 seconds timeout for inference

  // Build Multipart Form-Data Payload Header & Footer
  String boundary = "----ESP32CAMGarbageBoundary" + String(millis());
  String head = "--" + boundary + "\r\n"
              + "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n"
              + "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  size_t totalPayloadLength = head.length() + fb->len + tail.length();
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  // Allocate contiguous buffer in memory
  uint8_t *buffer = (uint8_t *)malloc(totalPayloadLength);
  if (!buffer) {
    Serial.println("[ERROR] Failed to allocate RAM for HTTP multipart payload!");
    esp_camera_fb_return(fb);
    http.end();
    Serial.println("UNKNOWN");
    return false;
  }

  // Copy parts into buffer
  memcpy(buffer, head.c_str(), head.length());
  memcpy(buffer + head.length(), fb->buf, fb->len);
  memcpy(buffer + head.length() + fb->len, tail.c_str(), tail.length());

  Serial.println("[HTTP] Transmitting image payload to ML Classification API...");
  int httpCode = http.POST(buffer, totalPayloadLength);

  // Release memory allocations immediately
  free(buffer);
  esp_camera_fb_return(fb);

  // Evaluate Server HTTP Response
  if (httpCode == HTTP_CODE_OK || httpCode == 200) {
    String responseBody = http.getString();
    Serial.println("[API RESPONSE] Received JSON response:");
    Serial.println(responseBody);

    // Parse JSON using ArduinoJson
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<512> doc;
#endif

    DeserializationError jsonError = deserializeJson(doc, responseBody);

    if (!jsonError) {
      // Extract class and confidence values safely
      const char* rawClass = doc["class"] | doc["original_class"] | "unknown";
      const char* category = doc["category"] | doc["prediction"] | rawClass;
      
      float confidence = 0.0;
      if (doc.containsKey("confidence")) {
        confidence = doc["confidence"].as<float>();
      } else if (doc.containsKey("confidence_percent")) {
        confidence = doc["confidence_percent"].as<float>() / 100.0f;
      }

      Serial.println("\n-------------------------------------------------");
      Serial.printf(" Raw Model Class  : %s\n", rawClass);
      Serial.printf(" Mapped Category  : %s\n", category);
      Serial.printf(" Calculated Conf. : %.4f (%.1f%%)\n", confidence, confidence * 100.0f);
      Serial.println("-------------------------------------------------");

      // Validate Confidence Threshold
      String resultCategory = String(category);
      resultCategory.toLowerCase();

      if (confidence < CONFIDENCE_THRESHOLD) {
        Serial.printf("[GUARDRAIL] Confidence %.2f is below threshold %.2f -> Categorized as UNKNOWN.\n", confidence, CONFIDENCE_THRESHOLD);
        resultCategory = "unknown";
      }

      // Format clean command to Arduino UNO
      String commandString = "";
      if (resultCategory.indexOf("plastic") >= 0) {
        commandString = "PLASTIC";
      } else if (resultCategory.indexOf("paper") >= 0 || resultCategory.indexOf("cardboard") >= 0) {
        commandString = "PAPER";
      } else if (resultCategory.indexOf("metal") >= 0 || resultCategory.indexOf("glass") >= 0) {
        commandString = "METAL";
      } else if (resultCategory.indexOf("organic") >= 0 || resultCategory.indexOf("biological") >= 0) {
        commandString = "ORGANIC";
      } else {
        commandString = "UNKNOWN";
      }

      Serial.printf("[UART TRANSMIT] Sending to Arduino UNO: %s\n", commandString.c_str());
      
      // Output category command over UART Serial to Arduino UNO
      Serial.println(commandString);
    } 
    else {
      Serial.printf("[ERROR] JSON deserialization failed: %s\n", jsonError.c_str());
      Serial.println("UNKNOWN");
    }
  } 
  else {
    Serial.printf("[HTTP ERROR] Request failed, Status Code: %d\n", httpCode);
    if (httpCode > 0) {
      Serial.println(http.getString());
    }
    Serial.println("UNKNOWN");
  }

  http.end();
  return true;
}

// ============================================================================
// 5. SETUP & INITIALIZATION
// ============================================================================
void setup() {
  // Initialize Hardware UART Serial (Baud Rate 9600 matches Arduino UNO SoftwareSerial)
  Serial.begin(9600);
  delay(500);

  Serial.println();
  Serial.println("=========================================================");
  Serial.println(" ESP32-CAM Machine Learning Segregation Client Initializing");
  Serial.println("=========================================================");

  // Configure Hardware Pins
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  pinMode(TRIGGER_PIN, INPUT_PULLUP);

  // Configure OV2640 Camera Parameters
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

  // Optimize resolution based on PSRAM memory availability
  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;  // 640x480 resolution
    config.jpeg_quality = 10;           // High JPEG quality (lower = higher quality)
    config.fb_count     = 2;
    Serial.println("[INFO] PSRAM detected! Operating in VGA mode.");
  } else {
    config.frame_size   = FRAMESIZE_QVGA; // 320x240 resolution
    config.jpeg_quality = 12;
    config.fb_count     = 1;
    Serial.println("[INFO] Standard RAM detected! Operating in QVGA mode.");
  }

  // Initialize Camera Module
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[FATAL] Camera init failed with error code 0x%x\n", err);
    return;
  }
  Serial.println("[OK] OV2640 Camera driver initialized.");

  // Connect to Wi-Fi Network
  WiFi.mode(WIFI_STA);

  if (USE_STATIC_IP) {
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
      Serial.println("[WARNING] Static IP assignment failed! Reverting to DHCP.");
    }
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WI-FI] Connecting to Access Point ");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[OK] Wi-Fi Connected! Device Assigned IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WARNING] Initial Wi-Fi connection timed out. Will retry in loop.");
  }
}

// ============================================================================
// 6. MAIN EXECUTION LOOP
// ============================================================================
void loop() {
  // Trigger condition A: Hardware IR Sensor / Push Button triggered on GPIO 13 (Active LOW)
  if (digitalRead(TRIGGER_PIN) == LOW) {
    if (millis() - lastCaptureTime >= CAPTURE_DEBOUNCE_MS) {
      lastCaptureTime = millis();
      Serial.println("\n[TRIGGER] Hardware Object Trigger Activated!");
      captureAndClassifyWaste();
    }
  }

  delay(100);
}
