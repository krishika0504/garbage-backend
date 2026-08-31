# ESP32-CAM & Multi-Module Streaming Wiring Guide

Complete hardware assembly, pinout connections, camera module selection, power configuration, and streaming setup instructions for the **Smart Waste Segregation System**.

---

## 📷 Supported Camera Modules & Board Specifications

The system supports multiple ESP32 camera hardware board configurations and browser WebCam streaming:

| Camera Module Board | Sensor Type | Key Features | Default Pins (XCLK / PCLK / VSYNC / HREF / SIOD / SIOC) |
| :--- | :--- | :--- | :--- |
| **AI-Thinker ESP32-CAM** | OV2640 2MP | GPIO 4 Flash LED, SD Card Slot, PSRAM | `0 / 22 / 25 / 23 / 26 / 27` |
| **ESP-EYE** | OV2640 / OV3660 | Onboard Mic, Dual Buttons, PSRAM | `4 / 25 / 5 / 27 / 18 / 23` |
| **M5Stack PSRAM Camera** | OV2640 2MP | Grove I2C Connector, PSRAM | `27 / 21 / 22 / 26 / 25 / 23` |
| **TTGO T-Journal** | OV2640 2MP | OLED Display, External Antenna | `27 / 21 / 22 / 26 / 25 / 23` |
| **ESP32 WROVER-KIT** | OV2640 2MP | Dual-core LCD Kit, RGB LED | `21 / 22 / 25 / 23 / 26 / 27` |
| **Local Browser WebCam** | Integrated / USB | HTML5 `getUserMedia` Video Feed | Zero Hardware Required |

> [!TIP]
> To select your camera board in the ESP32 firmware sketch (`esp32_cam_waste_classifier.ino`), uncomment the corresponding `#define CAMERA_MODEL_*` definition at line 30 before uploading to your board.

---

## ⚡ Pin Connections & Wiring Table

### 1. ESP32-CAM to Ultrasonic Object Detection Sensor (HC-SR04)

> [!NOTE]
> Connect `TRIG` to GPIO 13 and `ECHO` to GPIO 12.

| HC-SR04 Pin | ESP32-CAM Pin | Power Rail |
| :--- | :--- | :--- |
| **VCC** | - | **5V Power Rail** |
| **GND** | **GND** | **Common GND** |
| **TRIG** | **GPIO 13** | - |
| **ECHO** | **GPIO 12** | - |

---

### 2. ESP32-CAM to Servo Motor (SG90 / MG996R)

| Servo Wire | ESP32-CAM Pin | Power Rail |
| :--- | :--- | :--- |
| **Red (VCC)** | - | **5V 2A Power Rail** |
| **Black/Brown (GND)** | **GND** | **Common GND** |
| **Yellow/Orange (PWM)**| **GPIO 14** | - |

---

### 3. FTDI Programmer Connections (Flashing Code Only)

> [!IMPORTANT]
> To flash the sketch using Arduino IDE, **GPIO 0 must be shorted to GND**. Disconnect GPIO 0 after flashing to run in normal mode.

| FTDI Adapter | ESP32-CAM Pin |
| :--- | :--- |
| **VCC (5V)** | **5V** |
| **GND** | **GND** |
| **TX** | **U0R (GPIO 3)** |
| **RX** | **U0T (GPIO 1)** |
| - | **GPIO 0 to GND (Only while uploading code)** |

---

## 📐 Mechanical Servo Angles & Actions

| Waste Category | AI Detection | Target Servo Position | Sorting Action |
| :--- | :--- | :--- | :--- |
| **Neutral** | Waiting for object | `90°` | Neutral Center Position |
| **Dry Waste** | Cardboard, Plastic, Paper | `0°` | Full 90° flip left to **DRY Bin** |
| **Wet Waste** | Organic, Food Waste | `180°` | Full 90° flip right to **WET Bin** |

---

## 💻 Arduino IDE Setup & Library Requirements

1. **Board Manager URL**:
   Add to Arduino IDE `Preferences -> Additional Boards Manager URLs`:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```

2. **Select Board**:
   - Board: `AI Thinker ESP32-CAM`
   - CPU Frequency: `240MHz (WiFi/BT)`
   - Flash Frequency: `80MHz`
   - Partition Scheme: `Huge APP (3MB No OTA/1MB SPIFFS)`
   - PSRAM: `Enabled`

3. **Install Required Libraries** (`Tools -> Manage Libraries`):
   - `ArduinoJson` by Benoit Blanchon (Version 6.x or 7.x)
   - `ESP32Servo` by Kevin Harrington

---

## 🛠️ Troubleshooting: ESP32-CAM Auto-Restarts / Resets when Flipped

If your ESP32-CAM automatically restarts when flipped or moved, check these primary causes:

1. **Voltage Sag & Brownout Reset (`ESP_RST_BROWNOUT`)**:
   * ESP32-CAM + Wi-Fi burst + OV2640 camera initializations demand high current spikes (up to 500mA+). When flipped, physical cable tension can drop input voltage below 2.8V.
   * **Fix**: Use a **dedicated 5V 2A power adapter**. Add a **470µF-1000µF capacitor** across 5V and GND.
2. **Camera Ribbon Cable FPC Latch**:
   * When flipping the board, the OV2640 ribbon cable can wiggle or loose contact, causing a hardware exception crash (`ESP_RST_PANIC`).
   * **Fix**: Reseat the camera module ribbon cable into the FPC socket and secure the black latch tightly.
3. **Loose Power/GND Jumper Wires**:
   * Micro-disconnections on Breadboard/Dupont wire contacts when moving or rotating.
   * **Fix**: Replace loose female-female Dupont wires with solid pin headers or solder wires directly.
4. **Exposed Pin Short-Circuits**:
   * Solder pins on the back of ESP32-CAM touching a metallic frame/table when upside down.
   * **Fix**: Insulate the bottom of the module using electrical tape or plastic housing.

