/**
 * ============================================================================
 * Arduino UNO Main Controller - Dual Servo Automatic Garbage Segregation System
 * ============================================================================
 * 
 * Hardware Architecture:
 * - Microcontroller: Arduino UNO R3
 * - Display: 16x2 LCD with I2C Module (SDA -> A4, SCL -> A5)
 * - Actuators: 2 x Servo Motors (Powered by External 5V Supply, Common GND)
 *     - Servo Motor 1 Signal -> Pin D9  (Controls Plastic / Paper Bin Gate)
 *     - Servo Motor 2 Signal -> Pin D10 (Controls Metal / Organic Bin Gate)
 * - Status LEDs (via 220Ω Current Limiting Resistors):
 *     - Green LED  -> Pin D2 (System Ready / Power ON)
 *     - Yellow LED -> Pin D3 (Detecting / Classifying / Sorting / Warning)
 *     - Red LED    -> Pin D4 (Bin Full / System Fault)
 * - Bin Fill-Level Sensors:
 *     - IR Sensor 1 -> Pin D5 (Bin Warning Level)
 *     - IR Sensor 2 -> Pin D6 (Bin Full Level)
 * - ESP32-CAM Serial Communication (SoftwareSerial):
 *     - Arduino Pin D7 (RX) <- ESP32-CAM TX (GPIO 1)
 *     - Arduino Pin D8 (TX) -> ESP32-CAM RX (GPIO 3)
 * 
 * Communication Protocol:
 * - Baud Rate: 9600 bps
 * - Command Strings received from ESP32-CAM:
 *     - "PLASTIC"  / "CMD:PLASTIC"
 *     - "PAPER"    / "CMD:PAPER"
 *     - "METAL"    / "CMD:METAL"
 *     - "ORGANIC"  / "CMD:ORGANIC"
 *     - "UNKNOWN"  / "CMD:UNKNOWN"
 * 
 * Required Libraries:
 * - Servo.h (Built-in Arduino library)
 * - LiquidCrystal_I2C.h (by Frank de Brabander)
 * - SoftwareSerial.h (Built-in Arduino library)
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <SoftwareSerial.h>

// ============================================================================
// 1. CONFIGURABLE HARDWARE PIN ASSIGNMENTS
// ============================================================================
const int PIN_GREEN_LED   = 2;   // Green LED (System Powered & Ready)
const int PIN_YELLOW_LED  = 3;   // Yellow LED (Detecting / Sorting / Warning)
const int PIN_RED_LED     = 4;   // Red LED (Bin Full / Error Fault)
const int PIN_IR_SENSOR_1 = 5;   // IR Sensor 1 (Warning Level Indicator)
const int PIN_IR_SENSOR_2 = 6;   // IR Sensor 2 (Bin Full Level Indicator)
const int PIN_ESP_RX      = 7;   // SoftwareSerial RX (Receives from ESP32-CAM TX)
const int PIN_ESP_TX      = 8;   // SoftwareSerial TX (Sends to ESP32-CAM RX)
const int PIN_SERVO_1     = 9;   // Servo Motor 1 PWM Signal (Plastic/Paper Gate)
const int PIN_SERVO_2     = 10;  // Servo Motor 2 PWM Signal (Metal/Organic Gate)

// ============================================================================
// 2. CONFIGURABLE SENSOR & SERVO BEHAVIOR
// ============================================================================
// IR Sensor Active Logic: Set to true if IR module outputs LOW when obstacle detected
const bool IR_ACTIVE_LOW = true;

// Configurable Servo Angles (Calibrate for your physical mechanism)
const int SERVO1_HOME         = 90;  // Servo 1 Default Home Position (Closed Gate)
const int SERVO1_SORT_PLASTIC = 45;  // Servo 1 Chute position for Plastic Waste
const int SERVO1_SORT_PAPER   = 135; // Servo 1 Chute position for Paper Waste

const int SERVO2_HOME         = 90;  // Servo 2 Default Home Position (Closed Gate)
const int SERVO2_SORT_METAL   = 45;  // Servo 2 Chute position for Metal Waste
const int SERVO2_SORT_ORGANIC = 135; // Servo 2 Chute position for Organic Waste

// Timing Parameters
const int SERVO_SWEEP_DELAY_MS = 15;   // Milliseconds per degree step (Smooth motion)
const int SERVO_HOLD_TIME_MS  = 2500; // Hold time in sorting position before closing

// ============================================================================
// 3. OBJECT INITIALIZATION & GLOBAL STATE
// ============================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2); // Change address to 0x3F if screen displays blank blocks
Servo servo1;
Servo servo2;
SoftwareSerial espSerial(PIN_ESP_RX, PIN_ESP_TX); // RX = D7, TX = D8

int currentServo1Angle = SERVO1_HOME;
int currentServo2Angle = SERVO2_HOME;
bool isBinFull = false;
String lastLCDLine1 = "";
String lastLCDLine2 = "";

// ============================================================================
// 4. LCD & LED HELPER FUNCTIONS
// ============================================================================

/**
 * Control status LEDs cleanly.
 * @param green  State for Green LED
 * @param yellow State for Yellow LED
 * @param red    State for Red LED
 */
void setLEDs(bool green, bool yellow, bool red) {
  digitalWrite(PIN_GREEN_LED,  green  ? HIGH : LOW);
  digitalWrite(PIN_YELLOW_LED, yellow ? HIGH : LOW);
  digitalWrite(PIN_RED_LED,    red    ? HIGH : LOW);
}

/**
 * Helper to update LCD display cleanly without unnecessary flickering.
 */
void updateLCD(const String& line1, const String& line2) {
  if (line1 == lastLCDLine1 && line2 == lastLCDLine2) return; // Prevent flicker if unchanged
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  
  lastLCDLine1 = line1;
  lastLCDLine2 = line2;
}

/**
 * Helper to check if an IR sensor is triggered based on configured active logic.
 */
bool isIRTriggered(int pin) {
  int reading = digitalRead(pin);
  return IR_ACTIVE_LOW ? (reading == LOW) : (reading == HIGH);
}

// ============================================================================
// 5. SMOOTH SERVO CONTROL
// ============================================================================

/**
 * Moves a specified servo motor smoothly step-by-step to avoid mechanical shock
 * and sudden current spikes on the external power supply.
 */
void moveServoSmooth(Servo &targetServo, int &currentAngle, int targetAngle) {
  targetAngle = constrain(targetAngle, 0, 180);
  
  if (targetAngle > currentAngle) {
    for (int pos = currentAngle; pos <= targetAngle; pos++) {
      targetServo.write(pos);
      delay(SERVO_SWEEP_DELAY_MS);
    }
  } else if (targetAngle < currentAngle) {
    for (int pos = currentAngle; pos >= targetAngle; pos--) {
      targetServo.write(pos);
      delay(SERVO_SWEEP_DELAY_MS);
    }
  }
  
  currentAngle = targetAngle;
}

/**
 * Returns both servos to their default home positions safely.
 */
void resetServosToHome() {
  if (currentServo1Angle != SERVO1_HOME) {
    moveServoSmooth(servo1, currentServo1Angle, SERVO1_HOME);
  }
  if (currentServo2Angle != SERVO2_HOME) {
    moveServoSmooth(servo2, currentServo2Angle, SERVO2_HOME);
  }
}

// ============================================================================
// 6. BIN FILL-LEVEL MONITORING (PRIORITY CHECK)
// ============================================================================

/**
 * Continuously checks IR fill level sensors with the following priority:
 * 1. If both IR 1 and IR 2 indicate full -> Set bin full flag, block sorting, turn Red LED ON.
 * 2. If only IR 1 is triggered -> Display warning on LCD.
 * 3. If neither sensor is triggered -> Normal ready state, Green LED ON.
 */
bool evaluateBinFillLevels() {
  bool ir1Triggered = isIRTriggered(PIN_IR_SENSOR_1);
  bool ir2Triggered = isIRTriggered(PIN_IR_SENSOR_2);

  if (ir1Triggered && ir2Triggered) {
    // Priority 1: Bin Full Condition
    isBinFull = true;
    setLEDs(false, false, true); // Red LED ON, Green/Yellow OFF
    updateLCD("BIN FULL", "PLEASE EMPTY BIN");
    return true; // Indicates full
  } 
  else if (ir1Triggered) {
    // Priority 2: Bin Warning Level
    isBinFull = false;
    setLEDs(false, true, false); // Yellow LED ON for warning indication
    updateLCD("BIN WARNING", "LEVEL NEAR FULL");
    return false;
  } 
  else {
    // Priority 3: Normal Available Space
    isBinFull = false;
    setLEDs(true, false, false); // Green LED ON
    updateLCD("SYSTEM READY", "WAITING FOR ITEM");
    return false;
  }
}

// ============================================================================
// 7. GARBAGE CLASSIFICATION & SORTING EXECUTION
// ============================================================================

/**
 * Process received waste category, update display/LEDs, and actuate dual servos.
 * 
 * Workflow:
 * 1. Verify bin is not full.
 * 2. Turn Yellow LED ON (sorting in progress).
 * 3. Display category on LCD ("PLASTIC DETECTED", etc.).
 * 4. Display "SORTING..." on row 2.
 * 5. Move Servo 1 or Servo 2 to required angle.
 * 6. Hold for waste item to fall.
 * 7. Return servos to home position.
 * 8. Return system to Ready state (Green LED ON).
 */
void processGarbageSorting(String categoryCommand) {
  categoryCommand.toUpperCase();
  categoryCommand.trim();

  Serial.print(F("[SORTING PROCESS] Received Category Command: "));
  Serial.println(categoryCommand);

  // Check safety guardrail
  if (isBinFull) {
    Serial.println(F("[REJECTED] Cannot sort waste! Bin is FULL."));
    setLEDs(false, false, true); // Red LED ON
    updateLCD("BIN FULL", "SORTING CANCELLED");
    delay(2000);
    return;
  }

  // Active classification state -> Yellow LED ON
  setLEDs(false, true, false);

  if (categoryCommand.endsWith("PLASTIC") || categoryCommand == "PLASTIC") {
    Serial.println(F("[ACTION] Plastic classified -> Moving Servo 1 to Plastic Sorting Angle."));
    updateLCD("PLASTIC DETECTED", "SORTING...");
    moveServoSmooth(servo1, currentServo1Angle, SERVO1_SORT_PLASTIC);
    delay(SERVO_HOLD_TIME_MS);
    updateLCD("RETURNING HOME", "GATE CLOSING");
    moveServoSmooth(servo1, currentServo1Angle, SERVO1_HOME);
  }
  else if (categoryCommand.endsWith("PAPER") || categoryCommand == "PAPER") {
    Serial.println(F("[ACTION] Paper classified -> Moving Servo 1 to Paper Sorting Angle."));
    updateLCD("PAPER DETECTED", "SORTING...");
    moveServoSmooth(servo1, currentServo1Angle, SERVO1_SORT_PAPER);
    delay(SERVO_HOLD_TIME_MS);
    updateLCD("RETURNING HOME", "GATE CLOSING");
    moveServoSmooth(servo1, currentServo1Angle, SERVO1_HOME);
  }
  else if (categoryCommand.endsWith("METAL") || categoryCommand == "METAL") {
    Serial.println(F("[ACTION] Metal classified -> Moving Servo 2 to Metal Sorting Angle."));
    updateLCD("METAL DETECTED", "SORTING...");
    moveServoSmooth(servo2, currentServo2Angle, SERVO2_SORT_METAL);
    delay(SERVO_HOLD_TIME_MS);
    updateLCD("RETURNING HOME", "GATE CLOSING");
    moveServoSmooth(servo2, currentServo2Angle, SERVO2_HOME);
  }
  else if (categoryCommand.endsWith("ORGANIC") || categoryCommand == "ORGANIC") {
    Serial.println(F("[ACTION] Organic classified -> Moving Servo 2 to Organic Sorting Angle."));
    updateLCD("ORGANIC DETECTED", "SORTING...");
    moveServoSmooth(servo2, currentServo2Angle, SERVO2_SORT_ORGANIC);
    delay(SERVO_HOLD_TIME_MS);
    updateLCD("RETURNING HOME", "GATE CLOSING");
    moveServoSmooth(servo2, currentServo2Angle, SERVO2_HOME);
  }
  else if (categoryCommand.endsWith("UNKNOWN") || categoryCommand == "UNKNOWN") {
    Serial.println(F("[WARNING] Unknown class / Low confidence -> Keeping servos closed."));
    updateLCD("UNKNOWN", "NO AUTOMATIC SORT");
    setLEDs(false, true, false); // Yellow warning
    delay(2000);
    resetServosToHome();
  }
  else {
    Serial.println(F("[ERROR] Unrecognized command structure!"));
    updateLCD("SYSTEM ERROR", "UNHANDLED CMD");
    setLEDs(false, false, true); // Red LED ON
    delay(2000);
    resetServosToHome();
  }

  // Restore Green LED System Ready State
  setLEDs(true, false, false);
  updateLCD("SYSTEM READY", "WAITING FOR ITEM");
  delay(500);
}

// ============================================================================
// 8. SETUP & INITIALIZATION
// ============================================================================
void setup() {
  // Initialize Hardware Serial for Debugging Output to PC Monitor
  Serial.begin(9600);
  
  // Initialize SoftwareSerial for Communication with ESP32-CAM
  espSerial.begin(9600);

  Serial.println(F("========================================================="));
  Serial.println(F(" Automatic Garbage Segregation System - Arduino UNO R3"));
  Serial.println(F("========================================================="));

  // Configure Pin Modes
  pinMode(PIN_GREEN_LED, OUTPUT);
  pinMode(PIN_YELLOW_LED, OUTPUT);
  pinMode(PIN_RED_LED, OUTPUT);

  pinMode(PIN_IR_SENSOR_1, INPUT);
  pinMode(PIN_IR_SENSOR_2, INPUT);

  // Initialize LEDs (System Boot Test)
  setLEDs(true, true, true);
  delay(600);
  setLEDs(false, false, false);

  // Initialize 16x2 LCD Display
  lcd.init();
  lcd.backlight();
  updateLCD("INITIALIZING...", "GARBAGE SYSTEM");
  delay(1000);

  // Attach Servo Motors
  servo1.attach(PIN_SERVO_1);
  servo2.attach(PIN_SERVO_2);

  // Move Servos to Home Position
  servo1.write(SERVO1_HOME);
  servo2.write(SERVO2_HOME);
  currentServo1Angle = SERVO1_HOME;
  currentServo2Angle = SERVO2_HOME;
  delay(800);

  // Check initial fill levels
  evaluateBinFillLevels();
  
  Serial.println(F("[OK] Arduino UNO Main Controller initialized successfully."));
}

// ============================================================================
// 9. MAIN EXECUTION LOOP
// ============================================================================
void loop() {
  // Step 1: Periodically evaluate IR Fill-Level Sensors
  bool full = evaluateBinFillLevels();

  // Step 2: Read incoming classification commands from ESP32-CAM via SoftwareSerial
  if (espSerial.available() > 0) {
    String incomingData = espSerial.readStringUntil('\n');
    incomingData.trim();

    if (incomingData.length() > 0) {
      Serial.print(F("[ESP32-CAM UART] Received Payload: "));
      Serial.println(incomingData);

      // Flash Yellow LED and display "DETECTING..." briefly
      setLEDs(false, true, false);
      updateLCD("DETECTING...", "PROCESSING INFO");
      delay(800);

      if (full) {
        Serial.println(F("[BLOCKED] Bin is FULL! Ignoring sorting command."));
        updateLCD("BIN FULL", "COMMAND BLOCKED");
        setLEDs(false, false, true);
        delay(2000);
      } else {
        processGarbageSorting(incomingData);
      }
    }
  }

  delay(150); // Small stability delay
}
