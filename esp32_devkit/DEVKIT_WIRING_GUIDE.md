# ESP32 DevKit V1 Smart Bin Controller Wiring & Assembly Guide

Complete hardware assembly, pin connections, LCD I2C setup, and Arduino IDE configuration guide for the **ESP32 DevKit V1 Controller**.

---

## 🛠️ Required Components

| Quantity | Component | Purpose / Specification |
| :--- | :--- | :--- |
| 1 | **ESP32 DevKit V1 Board** | 30-pin or 38-pin ESP-WROOM-32 Microcontroller |
| 1 | **SG90 / MG996R Servo Motor** | Single Diverter Flap (90° Neutral Center, 0° Full Dry Left, 180° Full Wet Right) |
| 2 | **IR Proximity Sensor Modules** | Bin Level Sensors (Active LOW when Full) |
| 1 | **LCD 1602 Display with HW-61 Backpack** | I2C 16x2 Display (PCF8574 I2C adapter) |
| 3 | **5mm LEDs (Red, Yellow, Green)** | System Power (Red), Dry Full (Yellow), Wet Full (Green) |
| 3 | **220Ω Resistors** | Current limiting resistors for LEDs |
| 1 | **5V Active / Passive Buzzer Module** | Audio Feedback Alerts (Startup, Sorting, Bin Full) |
| 1 | **5V 2A External Power Adapter** | Dedicated power source for Servos & Sensors |

---

## 🔌 Circuit Pinout Table

### 1. LED Status Light Connections

| LED Color | Meaning / Function | ESP32 Pin | Anode (+) Pin Connection | Cathode (-) Pin Connection |
| :--- | :--- | :--- | :--- | :--- |
| 🔴 **Red LED** | System Power Indicator | **GPIO 23** | GPIO 23 via 220Ω Resistor | GND |
| 🟡 **Yellow LED** | Dry Bin Full Warning | **GPIO 2** | GPIO 2 via 220Ω Resistor | GND |
| 🟢 **Green LED** | Wet Bin Full Warning | **GPIO 4** | GPIO 4 via 220Ω Resistor | GND |

### 2. IR Sensor Module Connections

| IR Sensor Module | Function | Module Pin | ESP32 Pin / Power |
| :--- | :--- | :--- | :--- |
| **Dry Bin IR Sensor** | Dry Dustbin Full Level | OUT | **GPIO 34 (Input)** |
| | | VCC | **5V (Power)** |
| | | GND | **GND** |
| **Wet Bin IR Sensor** | Wet Dustbin Full Level | OUT | **GPIO 35 (Input)** |
| | | VCC | **5V (Power)** |
| | | GND | **GND** |

### 3. Single Servo Motor Connection

| Servo Motor | Function | Signal Pin (Yellow/Orange) | VCC (Red) | GND (Brown/Black) |
| :--- | :--- | :--- | :--- | :--- |
| **Servo 1 (Sorting Flap)** | Single Sorting Flap (Dry/Wet) | **GPIO 18 (PWM)** | 5V External | Common GND |

### 4. Audio Buzzer Connection

| Component | Function | Positive (+) / Signal Pin | Negative (-) / GND Pin |
| :--- | :--- | :--- | :--- |
| **5V Active Buzzer** | Audio Alert Output | **GPIO 25** | **GND** |

### 5. LCD 1602 Display + HW-61 I2C Backpack Connections

| HW-61 I2C Pin | Function | ESP32 DevKit V1 Pin |
| :--- | :--- | :--- |
| **SDA** | I2C Data Line | **GPIO 21** |
| **SCL** | I2C Clock Line | **GPIO 22** |
| **VCC** | Power Supply | **5V** |
| **GND** | Ground | **GND** |

---

## ⚡ Important Power Supply Rules

1. **Common Ground**: Always connect the **GND** of the ESP32 DevKit, the external 5V power supply, and the Servo motors together.
2. **External Power for Servos**: Servos draw up to 500mA-1A under load. **Do NOT power Servos directly from the ESP32 3.3V pin**; use an external 5V 2A power adapter or VIN pin!

---

## 📦 Required Arduino IDE Libraries

In Arduino IDE, open **Tools -> Manage Libraries...** and install:
1. `LiquidCrystal I2C` by Frank de Brabander (for LCD 1602 HW-61 backpack).
2. `ESP32Servo` by Kevin Harrington.
3. `ArduinoJson` by Benoit Blanchon (v6.x or v7.x).

---

## 🛠️ Troubleshooting: ESP32 Auto-Restarts / Resets when Flipped or Moved

If your ESP32 automatically restarts or reboots when you physically flip, tilt, or move it, here are the exact causes and hardware fixes:

### 1. 🔌 Loose USB Cable / Loose Jumper Wires (Micro-Disconnections)
* **Cause**: Breadboards and Dupont jumper cables have spring contacts that shift under motion. A momentary loss of power or GND connection causes an instant hard reset.
* **Fix**:
  * Ensure USB cable is plugged in firmly or replace standard jumper wires with tight solid-core wires.
  * Check that all GND connections are securely tied together (**Common Ground**).

### 2. ⚡ Voltage Drop & Brownout Reset (`ESP_RST_BROWNOUT`)
* **Cause**: When flipped, physical strain on servo cables or sensor position changes can cause momentary current spikes (500mA - 1A). If powered via a PC USB port or weak adapter, voltage dips below 2.8V, triggering the ESP32 hardware Brownout Detector reset.
* **Fix**:
  * Use a **dedicated 5V 2A external power supply** rather than powering from laptop/USB hub.
  * Solder or place a **470µF to 1000µF electrolytic capacitor** across `5V` and `GND` near the ESP32 power pins to buffer voltage drops.

### 3. 📌 Floating or Noisy EN (Reset) Pin
* **Cause**: The `EN` (Reset) pin is active-LOW. Physical motion, static electricity, or loose wires near `EN` can pull it LOW or induce noise, resetting the board.
* **Fix**:
  * Connect a **10kΩ pull-up resistor** between `EN` and `3.3V`.
  * Place a **100nF (0.1µF) ceramic capacitor** between `EN` and `GND` to filter noise.

### 4. ⚡ Short Circuit on Exposed Bottom Pins
* **Cause**: When flipped upside down, raw solder pins on the underside of the ESP32 DevKit or LCD module might touch a conductive surface (metal table, foil, loose wire leads).
* **Fix**:
  * Mount board on an insulated plastic base, 3D printed case, or cover the bottom with electrical tape.

