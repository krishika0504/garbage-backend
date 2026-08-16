/**
 * ESP32 DevKit - 2-Container Waste Segregation (Dry Waste vs Wet Waste)
 * 
 * Hardware Components:
 * 1. ESP32 DevKit V1 Board
 * 2. MG90S / SG90 Servo Motor (Signal -> GPIO 18)
 * 3. IR Proximity Sensor (OUT -> GPIO 13)
 * 4. Onboard LED (GPIO 2)
 * 
 * Container Mapping:
 * - Home Position : 90° (Center neutral chute)
 * - Container 1   : 45° (Dry Waste - Plastic, Paper, Metal, Glass, Trash)
 * - Container 2   : 135° (Wet Waste - Organic / Biological Food Waste)
 * 
 * Required Libraries (Arduino IDE):
 * - ESP32Servo (by Kevin Harrington)
 * - ArduinoJson (by Benoit Blanchon)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ================================================================
// 1. PIN & HARDWARE CONFIGURATION
// ================================================================
#define IR_SENSOR_PIN   13   // IR Sensor Digital Output Pin (Active LOW)
#define SERVO_PIN       18   // MG90S Servo PWM Signal Pin (GPIO 18)
#define LED_STATUS_PIN   2   // Onboard LED Indicator (GPIO 2)

const int SERVO_HOME_ANGLE = 90;  // Center neutral position
const int SERVO_DRY_ANGLE  = 45;  // Dry waste bin angle (Left chute)
const int SERVO_WET_ANGLE  = 135; // Wet waste bin angle (Right chute)

// Network Configuration
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Static IP Configuration
const bool USE_STATIC_IP  = true;
IPAddress local_IP(192, 168, 86, 201);  // Fixed IP for ESP32 DevKit
IPAddress gateway(192, 168, 86, 72);    // Router Gateway IP
IPAddress subnet(255, 255, 255, 0);     // Subnet Mask
IPAddress primaryDNS(8, 8, 8, 8);       // Primary DNS

Servo wasteServo;
int currentServoAngle = SERVO_HOME_ANGLE;

// Debounce & timing
unsigned long lastTriggerTime = 0;
const unsigned long DEBOUNCE_DELAY = 4000; // Cooldown (4 sec) between detections

// ================================================================
// 2. SMOOTH SERVO MOVEMENT CONTROLLER
// ================================================================
void moveServoSmooth(int targetAngle, int stepDelayMs = 15) {
  targetAngle = constrain(targetAngle, 0, 180);
  
  Serial.printf("[SERVO] Rotating from %d° to %d°...\n", currentServoAngle, targetAngle);
  
  if (targetAngle > currentServoAngle) {
    for (int pos = currentServoAngle; pos <= targetAngle; pos++) {
      wasteServo.write(pos);
      delay(stepDelayMs);
    }
  } else {
    for (int pos = currentServoAngle; pos >= targetAngle; pos--) {
      wasteServo.write(pos);
      delay(stepDelayMs);
    }
  }
  
  currentServoAngle = targetAngle;
}

// Executes full waste sorting sequence
void sortWasteSequence(int targetAngle) {
  digitalWrite(LED_STATUS_PIN, HIGH);
  
  // 1. Rotate servo to target bin angle
  moveServoSmooth(targetAngle, 12);
  
  // 2. Pause to allow item to fall into bin
  Serial.println("[SERVO] Holding position for waste item drop...");
  delay(2000); 
  
  // 3. Return back to HOME position (90°)
  Serial.println("[SERVO] Returning to Home position (90°)...");
  moveServoSmooth(SERVO_HOME_ANGLE, 12);
  
  digitalWrite(LED_STATUS_PIN, LOW);
}

// ================================================================
// 3. SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("==================================================");
  Serial.println(" ESP32 DevKit - Servo & IR Sensor Waste Controller");
  Serial.println("==================================================");

  // Configure Pins
  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);
  pinMode(LED_STATUS_PIN, OUTPUT);
  digitalWrite(LED_STATUS_PIN, LOW);

  // Allocate Timer & Attach Servo
  ESP32PWM::allocateTimer(0);
  wasteServo.setPeriodHertz(50);             // Standard 50Hz Servo frequency
  wasteServo.attach(SERVO_PIN, 500, 2400);   // MG90S pulse widths (500us - 2400us)
  
  // Initialize Servo at Home Position
  wasteServo.write(SERVO_HOME_ANGLE);
  delay(500);
  Serial.println("[OK] Servo initialized at Home position (90°).");

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  if (USE_STATIC_IP) {
    WiFi.config(local_IP, gateway, subnet, primaryDNS);
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("[OK] Connected to Wi-Fi! Device IP: %s\n", WiFi.localIP().toString().c_str());
}

// ================================================================
// 4. MAIN LOOP
// ================================================================
void loop() {
  // 1. Read IR Proximity Sensor (Active LOW: LOW = Object Present)
  int irState = digitalRead(IR_SENSOR_PIN);

  if (irState == LOW && (millis() - lastTriggerTime > DEBOUNCE_DELAY)) {
    lastTriggerTime = millis();
    Serial.println("\n[IR SENSOR] Object detected in drop chute!");

    // Example action: Notify ESP32-CAM or execute default sort
    // sortWasteSequence(30); 
  }

  // 2. Read Commands over UART Serial (from ESP32-CAM TX pin)
  // Format sent by ESP32-CAM: "ANGLE:30\n"
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("ANGLE:")) {
      int targetAngle = input.substring(6).toInt();
      Serial.printf("[UART COMMAND] Received target angle: %d°\n", targetAngle);
      sortWasteSequence(targetAngle);
    }
  }

  delay(50);
}
