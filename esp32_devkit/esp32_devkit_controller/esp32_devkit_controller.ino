/**
 * ============================================================================
 * ESP32 DevKit - Smart Waste Segregation System
 * ============================================================================
 *
 * Hardware:
 *   IR Sensor       -> GPIO 34
 *   Servo Motor     -> GPIO 18
 *   Red LED         -> GPIO 23
 *   Yellow LED      -> GPIO 2
 *   Green LED       -> GPIO 4
 *   Buzzer          -> GPIO 25
 *   LCD SDA         -> GPIO 21
 *   LCD SCL         -> GPIO 22
 *
 * IR SENSOR:
 *   LOW  = Bin FULL
 *   HIGH = Bin NOT FULL
 *
 * Waste Classification:
 *   Firebase Prediction -> Dry/Wet -> Servo
 *
 * Servo:
 *   Neutral = 69°
 *   Dry     = 10°
 *   Wet     = 160°
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ============================================================================
// 1. Wi-Fi & Firebase Configuration
// ============================================================================

const char *WIFI_SSID = "aniket";
const char *WIFI_PASSWORD = "12345678";

const char *FIREBASE_DB_HOST =
    "https://garbage-fa1b3-default-rtdb.firebaseio.com";

// ============================================================================
// 2. Hardware Pin Definitions
// ============================================================================

#define LED_POWER_RED      23
#define LED_DRY_FULL_YEL   2
#define LED_WET_FULL_GRN   4

// SINGLE IR SENSOR
#define SENSOR_IR          34

#define SERVO_PIN          18
#define BUZZER_PIN         25

#define I2C_SDA_PIN        21
#define I2C_SCL_PIN        22

// ============================================================================
// 3. Servo Positions
// ============================================================================

#define SERVO_NEUTRAL_ANGLE 69
#define SERVO_DRY_ACTIVATE  10
#define SERVO_WET_ACTIVATE  160

// ============================================================================
// 4. Global Objects
// ============================================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo segregateServo;

// ============================================================================
// 5. Timing
// ============================================================================

unsigned long lastHeartbeatTime = 0;
unsigned long lastPredictionPoll = 0;

const unsigned long HEARTBEAT_INTERVAL = 10000;
const unsigned long POLL_INTERVAL = 1500;

// ============================================================================
// 6. State Variables
// ============================================================================

String lastProcessedKey = "";
String lastManualCmdId = "";

bool binFullState = false;

// ============================================================================
// 7. Function Declarations
// ============================================================================

void initLCD();
void checkBinSensor();
void initializeBinSensorState();

void sendHeartbeat();
void pollLatestPrediction();
void pollManualControl();

void actuateSorting(String wasteClass, int angle);
void setServoAngle(int angle);

void playBuzzer(int durationMs);
void playBuzzerAlert(int pulses);

void printResetReason();

// ============================================================================
// SETUP
// ============================================================================

void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("[=========================================]");
  Serial.println("[*] ESP32 DevKit Smart Bin Controller");
  Serial.println("[=========================================]");

  printResetReason();

  // --------------------------------------------------------------------------
  // 1. LEDs & Buzzer
  // --------------------------------------------------------------------------

  pinMode(LED_POWER_RED, OUTPUT);
  pinMode(LED_DRY_FULL_YEL, OUTPUT);
  pinMode(LED_WET_FULL_GRN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_POWER_RED, HIGH);
  digitalWrite(LED_DRY_FULL_YEL, LOW);
  digitalWrite(LED_WET_FULL_GRN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // --------------------------------------------------------------------------
  // 2. Single IR Sensor
  // --------------------------------------------------------------------------

  pinMode(SENSOR_IR, INPUT);

  Serial.println("[+] Single IR Sensor: GPIO 34");

  // --------------------------------------------------------------------------
  // 3. Servo
  // --------------------------------------------------------------------------

  ESP32PWM::allocateTimer(1);

  segregateServo.setPeriodHertz(50);

  segregateServo.attach(
    SERVO_PIN,
    500,
    2400
  );

  segregateServo.write(SERVO_NEUTRAL_ANGLE);

  // --------------------------------------------------------------------------
  // 4. LCD
  // --------------------------------------------------------------------------

  Wire.begin(
    I2C_SDA_PIN,
    I2C_SCL_PIN
  );

  initLCD();

  lcd.setCursor(0, 0);
  lcd.print("SMART WASTE SYS ");

  lcd.setCursor(0, 1);
  lcd.print("WiFi Connecting");

  // --------------------------------------------------------------------------
  // 5. Wi-Fi
  // --------------------------------------------------------------------------

  Serial.printf(
    "[*] Connecting to Wi-Fi '%s' ",
    WIFI_SSID
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();
  Serial.println("[+] Wi-Fi Connected!");

  Serial.print("[+] ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // Configure Google Public DNS for reliable Firebase access
  IPAddress dns1(8, 8, 8, 8);
  IPAddress dns2(1, 1, 1, 1);
  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
  Serial.printf("[+] Public DNS Configured: %s, %s\n", WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());

  // --------------------------------------------------------------------------
  // 6. Startup Buzzer
  // --------------------------------------------------------------------------

  playBuzzerAlert(2);

  // --------------------------------------------------------------------------
  // 7. IMPORTANT:
  // Initialize IR state immediately.
  // This prevents "BIN: CHECKING" from remaining on LCD.
  // --------------------------------------------------------------------------

  initializeBinSensorState();

  delay(1000);

  // Show system ready
  lcd.setCursor(0, 0);
  lcd.print("SYS: ONLINE     ");

  // Keep actual bin state on second line
  lcd.setCursor(0, 1);

  if (binFullState) {
    lcd.print("BIN: FULL       ");
  } else {
    lcd.print("BIN: OK         ");
  }
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {

  // --------------------------------------------------------------------------
  // 1. Check IR Sensor
  // --------------------------------------------------------------------------

  checkBinSensor();

  // --------------------------------------------------------------------------
  // 2. Heartbeat
  // --------------------------------------------------------------------------

  if (
    millis() - lastHeartbeatTime >=
    HEARTBEAT_INTERVAL
  ) {

    lastHeartbeatTime = millis();

    sendHeartbeat();
  }

  // --------------------------------------------------------------------------
  // 3. Firebase Prediction + Manual Control
  // --------------------------------------------------------------------------

  if (
    millis() - lastPredictionPoll >=
    POLL_INTERVAL
  ) {

    lastPredictionPoll = millis();

    pollLatestPrediction();

    pollManualControl();
  }

  delay(100);
}

// ============================================================================
// LCD INITIALIZATION
// ============================================================================

void initLCD() {

  lcd.init();

  lcd.backlight();

  lcd.clear();
}

// ============================================================================
// INITIALIZE IR SENSOR STATE
// ============================================================================

void initializeBinSensorState() {

  bool currentBinFull =
      (digitalRead(SENSOR_IR) == LOW);

  binFullState =
      currentBinFull;

  // Update LEDs immediately
  digitalWrite(
    LED_DRY_FULL_YEL,
    binFullState ? HIGH : LOW
  );

  digitalWrite(
    LED_WET_FULL_GRN,
    binFullState ? HIGH : LOW
  );

  // Update LCD immediately
  lcd.setCursor(0, 1);

  if (binFullState) {

    lcd.print("BIN: FULL       ");

    Serial.println(
      "[!] INITIAL IR STATUS: BIN FULL"
    );

  } else {

    lcd.print("BIN: OK         ");

    Serial.println(
      "[+] INITIAL IR STATUS: BIN OK"
    );
  }
}

// ============================================================================
// CHECK SINGLE IR SENSOR
// ============================================================================

void checkBinSensor() {

  /*
   * GPIO 34:
   *
   * LOW  = Object detected / Bin FULL
   * HIGH = No object / Bin OK
   */

  bool currentBinFull =
      (digitalRead(SENSOR_IR) == LOW);

  // Nothing changed
  if (
    currentBinFull ==
    binFullState
  ) {
    return;
  }

  // Update state
  binFullState =
      currentBinFull;

  // --------------------------------------------------------------------------
  // LEDs
  // --------------------------------------------------------------------------

  digitalWrite(
    LED_DRY_FULL_YEL,
    binFullState ? HIGH : LOW
  );

  digitalWrite(
    LED_WET_FULL_GRN,
    binFullState ? HIGH : LOW
  );

  // --------------------------------------------------------------------------
  // LCD
  // --------------------------------------------------------------------------

  lcd.setCursor(0, 1);

  if (binFullState) {

    lcd.print("BIN: FULL       ");

    Serial.println(
      "[!] IR SENSOR: BIN FULL"
    );

    playBuzzerAlert(3);

  } else {

    lcd.print("BIN: OK         ");

    Serial.println(
      "[+] IR SENSOR: BIN OK"
    );
  }

  // --------------------------------------------------------------------------
  // Firebase
  // --------------------------------------------------------------------------

  if (WiFi.status() == WL_CONNECTED) {

    WiFiClientSecure client;

    client.setInsecure();

    HTTPClient http;

    String url =
        String(FIREBASE_DB_HOST) +
        "/bin_status.json";

    http.begin(
      client,
      url
    );

    http.addHeader(
      "Content-Type",
      "application/json"
    );

    String payload =
        "{\"bin_full\":" +
        String(
          binFullState
          ? "true"
          : "false"
        ) +

        ",\"fill_pct\":" +
        String(
          binFullState
          ? 100
          : 25
        ) +

        ",\"timestamp\":{\".sv\":\"timestamp\"}}";

    int httpCode =
        http.sendRequest(
          "PUT",
          (uint8_t *)payload.c_str(),
          payload.length()
        );

    Serial.printf(
      "[Firebase] Bin Status HTTP: %d\n",
      httpCode
    );

    http.end();
  }
}

// ============================================================================
// SEND HEARTBEAT
// ============================================================================

void sendHeartbeat() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(4);

  HTTPClient http;
  String heartbeatUrl =
      String(FIREBASE_DB_HOST) +
      "/hardware_heartbeats/esp32_devkit.json";

  http.begin(
    client,
    heartbeatUrl
  );
  http.setTimeout(3500);

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  String payload =
      "{\"device\":\"esp32_devkit\",\"ip\":\"" +
      WiFi.localIP().toString() +
      "\",\"status\":\"ONLINE\",\"rssi\":" +
      String(WiFi.RSSI()) +
      ",\"timestamp\":{\".sv\":\"timestamp\"}}";

  int httpCode = http.PUT(payload);

  if (httpCode > 0) {
    Serial.printf(
      "[+] Heartbeat Updated | IP: %s\n",
      WiFi.localIP().toString().c_str()
    );
  } else {
    Serial.printf("[!] Heartbeat failed: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
  }

  http.end();
}

// ============================================================================
// POLL LATEST CLASSIFICATION
// ============================================================================

void pollLatestPrediction() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(4);

  HTTPClient http;
  String pollUrl =
      String(FIREBASE_DB_HOST) +
      "/predictions.json?orderBy=%22$key%22&limitToLast=1";

  http.begin(
    client,
    pollUrl
  );
  http.setTimeout(3500);

  int httpCode = http.GET();

  if (httpCode == 200) {

    String response = http.getString();

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, response);

    if (!error && doc.is<JsonObject>()) {

      JsonObject obj = doc.as<JsonObject>();

      for (JsonPair kv : obj) {

        String currentKey = kv.key().c_str();
        JsonObject item = kv.value().as<JsonObject>();

        // New prediction
        if (currentKey != lastProcessedKey && !lastProcessedKey.equals("")) {

          lastProcessedKey = currentKey;

          const char *category = item["class"] | "Dry";
          int servoAngle = item["servo_angle"] | 0;

          Serial.printf(
            "\n[!] New Classification Received: %s (Servo: %d°)\n",
            category,
            servoAngle
          );

          actuateSorting(
            String(category),
            servoAngle
          );

        } else if (lastProcessedKey.equals("")) {

          lastProcessedKey = currentKey;
          Serial.printf("[+] Syncing latest Firebase key: %s (Listening for new classifications...)\n", lastProcessedKey.c_str());
        }
      }
    }
  } else if (httpCode > 0 && httpCode != 200) {
    Serial.printf("[Firebase] Poll error (HTTP %d)\n", httpCode);
  }

  http.end();
}

// ============================================================================
// MANUAL SERVO CONTROL
// ============================================================================

void pollManualControl() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  String manualUrl =
      String(FIREBASE_DB_HOST) +
      "/manual_control.json";

  http.begin(
    client,
    manualUrl
  );

  int httpCode =
      http.GET();

  if (httpCode == 200) {

    String response =
        http.getString();

    StaticJsonDocument<512> doc;

    DeserializationError error =
        deserializeJson(
          doc,
          response
        );

    if (
      !error &&
      doc.is<JsonObject>()
    ) {

      JsonObject obj =
          doc.as<JsonObject>();

      int targetAngle =
          obj["target_angle"] | -1;

      String cmdId = "";

      if (
        obj.containsKey("command_id")
      ) {

        cmdId =
            obj["command_id"].as<String>();

      } else if (
        obj.containsKey("timestamp")
      ) {

        cmdId =
            String(
              obj["timestamp"].as<uint64_t>()
            );
      }

      if (
        targetAngle >= 0 &&
        targetAngle <= 180 &&
        !cmdId.equals("")
      ) {

        if (
          !cmdId.equals(lastManualCmdId) &&
          !lastManualCmdId.equals("")
        ) {

          Serial.printf(
            "\n[!] Manual Servo Override: %d°\n",
            targetAngle
          );

          playBuzzer(150);

          lcd.setCursor(0, 0);
          lcd.print("MANUAL OVERRIDE ");

          lcd.setCursor(0, 1);
          lcd.print("Angle: ");
          lcd.print(targetAngle);
          lcd.print((char)223);
          lcd.print("      ");

          setServoAngle(
            targetAngle
          );

          lcd.setCursor(0, 0);
          lcd.print("SYS: ONLINE     ");

          lcd.setCursor(0, 1);

          if (binFullState) {
            lcd.print("BIN: FULL       ");
          } else {
            lcd.print("BIN: OK         ");
          }
        }

        lastManualCmdId =
            cmdId;
      }
    }
  }

  http.end();
}

// ============================================================================
// WASTE SORTING
// ============================================================================

void actuateSorting(
    String wasteClass,
    int angle
) {

  bool isWet =
      wasteClass.equalsIgnoreCase("Wet");

  int targetAngle = isWet ? SERVO_WET_ACTIVATE : SERVO_DRY_ACTIVATE;

  playBuzzer(200);

  lcd.setCursor(0, 0);

  lcd.print("SORT: ");

  if (isWet) {
    lcd.print("WET WASTE ");
  } else {
    lcd.print("DRY WASTE ");
  }

  Serial.printf(
    "[*] Servo -> %d° (%s Waste)\n",
    targetAngle,
    isWet ? "WET" : "DRY"
  );

  setServoAngle(
    targetAngle
  );

  // Restore normal LCD display
  lcd.setCursor(0, 0);
  lcd.print("SYS: ONLINE     ");

  lcd.setCursor(0, 1);

  if (binFullState) {
    lcd.print("BIN: FULL       ");
  } else {
    lcd.print("BIN: OK         ");
  }
}

// ============================================================================
// SERVO CONTROL
// ============================================================================

void setServoAngle(
    int angle
) {

  Serial.printf(
    "[*] Servo angle: %d°\n",
    angle
  );

  segregateServo.write(
    angle
  );

  if (
    angle !=
    SERVO_NEUTRAL_ANGLE
  ) {

    delay(2000);

    Serial.printf(
      "[*] Servo returning to %d°\n",
      SERVO_NEUTRAL_ANGLE
    );

    segregateServo.write(
      SERVO_NEUTRAL_ANGLE
    );
  }
}

// ============================================================================
// BUZZER
// ============================================================================

void playBuzzer(
    int durationMs
) {

  digitalWrite(
    BUZZER_PIN,
    HIGH
  );

  delay(
    durationMs
  );

  digitalWrite(
    BUZZER_PIN,
    LOW
  );
}

// ============================================================================
// BUZZER ALERT
// ============================================================================

void playBuzzerAlert(
    int pulses
) {

  for (
    int i = 0;
    i < pulses;
    i++
  ) {

    digitalWrite(
      BUZZER_PIN,
      HIGH
    );

    delay(100);

    digitalWrite(
      BUZZER_PIN,
      LOW
    );

    if (
      i < pulses - 1
    ) {

      delay(100);
    }
  }
}

// ============================================================================
// RESET REASON
// ============================================================================

void printResetReason() {

  esp_reset_reason_t reason =
      esp_reset_reason();

  Serial.print(
    "[SYS] Boot Reset Cause: "
  );

  switch (reason) {

    case ESP_RST_POWERON:
      Serial.println("Power-On Reset");
      break;

    case ESP_RST_EXT:
      Serial.println("External Reset");
      break;

    case ESP_RST_SW:
      Serial.println("Software Reset");
      break;

    case ESP_RST_PANIC:
      Serial.println("Exception/Panic Reset");
      break;

    case ESP_RST_INT_WDT:
      Serial.println("Interrupt Watchdog Reset");
      break;

    case ESP_RST_TASK_WDT:
      Serial.println("Task Watchdog Reset");
      break;

    case ESP_RST_WDT:
      Serial.println("Other Watchdog Reset");
      break;

    case ESP_RST_DEEPSLEEP:
      Serial.println("Deep Sleep Reset");
      break;

    case ESP_RST_BROWNOUT:
      Serial.println("BROWNOUT RESET!");
      break;

    case ESP_RST_SDIO:
      Serial.println("SDIO Reset");
      break;

    default:
      Serial.println("Unknown Reset");
      break;
  }
}