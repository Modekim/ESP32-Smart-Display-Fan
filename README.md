# ESP32 Smart Display + FAN Controller (Low Power)
![Platform](https://img.shields.io/badge/Platform-ESP32-blue)
![Framework](https://img.shields.io/badge/Framework-Arduino-green)
![Integration](https://img.shields.io/badge/Home%20Assistant-MQTT-orange)
![License](https://img.shields.io/badge/License-MIT-lightgrey)

**ESP32 + SSD1306 OLED + DS18B20 + MQTT (Home Assistant) + Solar Power + FAN (MOSFET, hysteresis)**

> Self-contained IoT node with OLED UI, Home Assistant integration, automatic fan control with hysteresis, EU DST time support, and multiple power optimizations. Designed as a portfolio project to demonstrate ESP32 programming, IoT integration, and low-power design.

---

## ✨ Key Features

- **OLED display (SSD1306 128×64, I²C)**.
- **MQTT integration with Home Assistant (HA)**.
- **DS18B20 temperature sensor** (local fan control + MQTT publish).
- **FAN control**: MOSFET low-side, **modes**: `AUTO` / `ON` / `OFF` with hysteresis.
- **Dynamic publish policy**: DS18B20 publishes only if  
  ΔT ≥ threshold **or** min interval elapsed → less MQTT traffic + lower power.
- **NTP time sync + automatic CET/CEST (Europe/Warsaw)**.
- **Low-power tweaks**:
  - Wi-Fi power save (`WIFI_PS_MAX_MODEM`).
  - TX power tuning based on RSSI.
  - Long redraw intervals on OLED.
- **LWT (Last Will & Testament)** – topic `esp32/availability` reports `online` / `offline`.
---

## 🖥️ OLED UI layout

```
Row 1:  <MQTT Temp °C>               <MQTT Humidity %>
Row 2:  <MQTT Pressure hPa>          FAN <ON|OFF>
Row 3:  <DS18B20 Temp (int) °C>      <Last MQTT update HH:MM>
```

---

## 🔌 MQTT Topics

### Inbound (HA → ESP32 → OLED)

> These topics are published by HA automations forwarding values from a single Aqara Weather sensor (temperature, humidity, pressure) — see Home Assistant Integration / Automations.

* `esp32/weather/temperature` → float °C (e.g. `23.4`)
* `esp32/weather/humidity` → integer % (e.g. `55`)
* `esp32/weather/pressure` → integer hPa (e.g. `1015`)

### Outbound (ESP32 → HA, retained)

* `esp32/fan/temperature`      → DS18B20 °C (e.g. `23.75`)
* `esp32/fan/state`            → `ON` | `OFF`
* `esp32/fan/mode`             → `AUTO`/`ON`/`OFF`
* `esp32/fan/config/on_c`      → float °C
* `esp32/fan/config/off_c`     → float °C
* `esp32/fan/config/delta_c`   → float °C (min ΔT for publish)
* `esp32/fan/config/min_ms`    → uint32 (min interval for publish, ms)

### Control (HA → ESP32, commands)

* `esp32/fan/cmd`                → `AUTO` | `ON` | `OFF`
* `esp32/fan/config/on_c/set`    → float °C
* `esp32/fan/config/off_c/set`   → float °C
* `esp32/fan/config/delta_c/set` → float °C (0.05–5.0)
* `esp32/fan/config/min_ms/set`  → uint32 ms (5 000–3 600 000)

### Availability (LWT)

* `esp32/availability` → `online` | `offline`

---

## 🧠 Fan Control Logic (Hysteresis)

- Default: **ON ≥ 30 °C**, **OFF ≤ 25 °C**.
- Change thresholds live via MQTT.
- DS18B20 publishes only if ΔT ≥ threshold (default 0.25 °C) or min interval (60 s).
- **Home Assistant ready** with retained states.

---

## 🔧 Hardware Overview

- **ESP32 DevKit (ESP-WROOM, CP2102, USB-C)**
- **OLED 0.96" SSD1306** (128×64, I²C)
- **DS18B20** (with 4.7 kΩ pull-up to 3.3 V)
- **N-MOSFET IRLZ44N** (GPIO23 gate) + flyback diode + passives
- **5 V cooling fan (3010)**
- **Solar supply**: 6 V / 1 W panel → Schottky diode → XL6009 (step-up) → LM2596S (step-down) → TP4056 → 2×18650 cells

📦 **Full detailed BOM is available in [`docs/BOM.md`](docs/BOM.md)**


---

## 🪛 ESP32 Pinout

| Function     | GPIO | Notes                |
|--------------|------|----------------------|
| OLED SDA     | 21   | I²C data             |
| OLED SCL     | 22   | I²C clock            |
| DS18B20 DATA | 4    | +4.7 kΩ pull-up to 3.3 V |
| FAN MOSFET   | 23   | Gate, HIGH = ON      |


---

## 🗂️ Repo Structure

```
ESP32-Smart-Display-Fan/
├─ ESP32-Smart-Display-Fan.ino
├─ config.example.h # committed template
├─ config.h # your secrets (NOT committed)
├─ docs/
│ └─ BOM.md # full hardware bill of materials
├─ README.md
├─ LICENSE
└─ .gitignore
```

---

## ⚙️ Configuration (Wi‑Fi / MQTT)

1. Copy `config.example.h` → **`config.h`** (same folder).
2. Fill in:
```cpp
   #define CFG_WIFI_SSID       "..."
   #define CFG_WIFI_PASSWORD   "..."
   #define CFG_MQTT_HOST       "..."
   #define CFG_MQTT_PORT       1883
   #define CFG_MQTT_USER       "..."
   #define CFG_MQTT_PASSWORD   "..."
```

Leave defaults for hysteresis / DS18B20 policy or adjust.

> Do not commit config.h. It’s ignored via .gitignore.

---

## ▶️ Build & Upload

### 1. Arduino IDE (ESP32 core)

1. Install libraries: `Adafruit_GFX`, `Adafruit_SSD1306`, `PubSubClient`, `OneWire`, `DallasTemperature`, `NTPClient`.
2. Select board **ESP32 Dev Module** and the correct COM port.
3. Open ESP32-Smart-Display-Fan.ino and upload.

### 2. PlatformIO (optional, recommended)

Create `platformio.ini`:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

monitor_speed = 115200
monitor_filters = esp32_exception_decoder

lib_deps =
  adafruit/Adafruit GFX Library
  adafruit/Adafruit SSD1306
  knolleary/PubSubClient
  paulstoffregen/OneWire
  milesburton/DallasTemperature
  arduino-libraries/NTPClient
```

Then build & upload:
```
pio run --target upload
pio device monitor
```

---

## 🧩 Home Assistant Integration

### 1. Automations: publish HA → ESP32  
Add these rules to your `automations.yaml` so HA forwards Aqara sensor data to the MQTT topics that ESP32 subscribes to (`esp32/weather/*`).  
Values are **rounded** and **retained**, with **10s debounce**

```yaml
# TEMPERATURE
- alias: "MQTT: publish temperature (retained) on change"
  trigger:
    - platform: state
      entity_id: sensor.lumi_lumi_weather_temperature
      for: "00:00:10"
  condition:
    - condition: template
      value_template: >
        {{ states('sensor.lumi_lumi_weather_temperature') | is_number
           and trigger.to_state.state != trigger.from_state.state }}
  action:
    - service: mqtt.publish
      data:
        topic: esp32/weather/temperature
        payload: "{{ states('sensor.lumi_lumi_weather_temperature') | float(0) | round(1) }}"
        retain: true
        qos: 1

- alias: "MQTT: publish temperature (retained) at HA startup"
  trigger:
    - platform: homeassistant
      event: start
  condition:
    - condition: template
      value_template: "{{ states('sensor.lumi_lumi_weather_temperature') | is_number }}"
  action:
    - service: mqtt.publish
      data:
        topic: esp32/weather/temperature
        payload: "{{ states('sensor.lumi_lumi_weather_temperature') | float(0) | round(1) }}"
        retain: true
        qos: 1

# HUMIDITY
- alias: "MQTT: publish humidity (retained) on change"
  trigger:
    - platform: state
      entity_id: sensor.lumi_lumi_weather_humidity
      for: "00:00:10"
  condition:
    - condition: template
      value_template: >
        {{ states('sensor.lumi_lumi_weather_humidity') | is_number
           and trigger.to_state.state != trigger.from_state.state }}
  action:
    - service: mqtt.publish
      data:
        topic: esp32/weather/humidity
        payload: "{{ states('sensor.lumi_lumi_weather_humidity') | float(0) | round(0) }}"
        retain: true
        qos: 1

- alias: "MQTT: publish humidity (retained) at HA startup"
  trigger:
    - platform: homeassistant
      event: start
  condition:
    - condition: template
      value_template: "{{ states('sensor.lumi_lumi_weather_humidity') | is_number }}"
  action:
    - service: mqtt.publish
      data:
        topic: esp32/weather/humidity
        payload: "{{ states('sensor.lumi_lumi_weather_humidity') | float(0) | round(0) }}"
        retain: true
        qos: 1

# PRESSURE
- alias: "MQTT: publish pressure (retained) on change"
  trigger:
    - platform: state
      entity_id: sensor.lumi_lumi_weather_pressure
      for: "00:00:10"
  condition:
    - condition: template
      value_template: >
        {{ states('sensor.lumi_lumi_weather_pressure') | is_number
           and trigger.to_state.state != trigger.from_state.state }}
  action:
    - service: mqtt.publish
      data:
        topic: esp32/weather/pressure
        payload: "{{ states('sensor.lumi_lumi_weather_pressure') | float(0) | round(0) }}"
        retain: true
        qos: 1

- alias: "MQTT: publish pressure (retained) at HA startup"
  trigger:
    - platform: homeassistant
      event: start
  condition:
    - condition: template
      value_template: "{{ states('sensor.lumi_lumi_weather_pressure') | is_number }}"
  action:
    - service: mqtt.publish
      data:
        topic: esp32/weather/pressure
        payload: "{{ states('sensor.lumi_lumi_weather_pressure') | float(0) | round(0) }}"
        retain: true
        qos: 1
```

### 2. Entities: ESP32 → HA (`configuration.yaml`)  
This section exposes ESP32 data and controls into Home Assistant via MQTT.  
It covers **sensors**, **binary sensors**, **select (mode)**, and **numbers (config parameters)**.

- **MQTT sensors** – show ESP32 internal values (DS18B20 temperature).  
- **MQTT binary sensors** – reflect fan state (ON/OFF) and node availability.  
- **MQTT select** – allows selecting fan mode: `AUTO` / `ON` / `OFF`.  
- **MQTT numbers** – expose configuration parameters (hysteresis thresholds, DS18B20 publish policy) as editable numeric inputs in HA UI.

```yaml
mqtt:
  sensor: 
    - name: "ESP32 Temperature"
      unique_id: esp32_fan_temperature
      state_topic: "esp32/fan/temperature"
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      value_template: "{{ value | float(0) }}"
      availability_topic: "esp32/availability"
      payload_available: "online"
      payload_not_available: "offline"
  
  binary_sensor:
    - name: "ESP32 Fan State"
      unique_id: esp32_fan_state
      state_topic: "esp32/fan/state"
      payload_on: "ON"
      payload_off: "OFF"
      device_class: power
      availability_topic: "esp32/availability"
      payload_available: "online"
      payload_not_available: "offline"

    - name: "ESP32 Node Online"
      unique_id: esp32_node_online
      state_topic: "esp32/availability"
      payload_on: "online"
      payload_off: "offline"
      device_class: connectivity
  
  select:
    - name: "ESP32 Fan Mode"
      unique_id: esp32_fan_mode
      state_topic: "esp32/fan/mode"
      command_topic: "esp32/fan/cmd"
      options:
        - "AUTO"
        - "ON"
        - "OFF"
      optimistic: false  
 
  number:
    - name: "ESP32 Fan ON °C"
      unique_id: esp32_fan_on_c
      state_topic: "esp32/fan/config/on_c"
      command_topic: "esp32/fan/config/on_c/set"
      min: 10
      max: 60
      step: 0.5
      unit_of_measurement: "°C"
      mode: box

    - name: "ESP32 Fan OFF °C"
      unique_id: esp32_fan_off_c
      state_topic: "esp32/fan/config/off_c"
      command_topic: "esp32/fan/config/off_c/set"
      min: 5
      max: 55
      step: 0.5
      unit_of_measurement: "°C"
      mode: box

    - name: "ESP32 DS18B20 Publish Δ"
      command_topic: "esp32/fan/config/delta_c/set"
      state_topic: "esp32/fan/config/delta_c"
      unit_of_measurement: "°C"
      device_class: temperature
      min: 0.05
      max: 5.0
      step: 0.05
      mode: box
      retain: true
      unique_id: "esp32_ds18b20_publish_delta"

    - name: "ESP32 DS18B20 Min Interval"
      command_topic: "esp32/fan/config/min_ms/set"
      state_topic: "esp32/fan/config/min_ms"
      unit_of_measurement: "ms"
      min: 5000
      max: 3600000
      step: 1000
      mode: box
      retain: true
      unique_id: "esp32_ds18b20_min_interval_ms"
```

### 3. Data Flow Overview

```text
[Aqara Weather Sensor (Temp/Humidity/Pressure)]
               │
               ▼
[ Home Assistant ] --(MQTT publish: esp32/weather/*, retained)--> [ ESP32 Display & Fan Controller ]
               ▲                                                            │
               │                                                            ▼
[ HA entities (sensor, binary_sensor, select, number) ] <----(MQTT state/config)--- ESP32
```

### 4. MQTT Test Commands

```bash
# Force fan ON
mosquitto_pub -t esp32/fan/cmd -m "ON"

# Change hysteresis ON threshold to 29.5 °C
mosquitto_pub -t esp32/fan/config/on_c/set -m "29.5"
```

---

## 🔒 Security & Reliability

* **LWT**: broker sets `offline` on `esp32/availability` if ESP32 disconnects.
* **No secrets in repo** — use config.h.
* **Backoff reconnect** for MQTT.
* **Invalid DS18B20 readings** ignored; last good value is retained.

---

## 📜 License

**MIT License © Rafał Gromulski**

---

## 👤 Author

Rafał Gromulski
Programming • IoT • Embedded systems • Electronics. Contact: [LinkedIn](https://www.linkedin.com/in/rafałgromulski) / [GitHub](https://github.com/RafalGromulski).

---
