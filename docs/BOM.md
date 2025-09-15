# Bill of Materials (BOM)

Detailed parts list for **ESP32 Smart Display + FAN Controller (Low Power)**.

---

## 🔋 Power Stage

| Part | Value / Type | Notes |
|------|--------------|-------|
| Solar panel | 6 V / 1 W | 136×110×3 mm |
| Diode | 1N5819 Schottky | Prevents backflow at night |
| Step-up converter | XL6009 | Boosts unstable panel voltage |
| Step-down converter | LM2596S | Regulates to 5 V for TP4056 |
| Charger | TP4056 (USB-C, protection) | 5 V input, Li-Ion charging |
| Battery | 2× INR18650-35E (parallel) | 3500 mAh cells |

---

## ⚡ Regulation & Distribution

| Part | Value / Type | Notes |
|------|--------------|-------|
| Step-up converter | XL6009 | 5 V rail for fan |
| Step-down converter | LM2596S | 5 V → 3.3 V (ESP32, DS18B20, OLED) |

---

## 🖥️ Control & Processing

| Part | Value / Type | Notes |
|------|--------------|-------|
| MCU | ESP32 DevKit (ESP-WROOM, CP2102, USB-C) | Core controller |
| Display | OLED 0.96" SSD1306 (128×64, I²C) | UI |
| Temp sensor | DS18B20 (TO-92) | 1-wire, 4.7 kΩ pull-up to 3.3 V |

---

## 🌬️ Cooling Stage (MOSFET Low-Side)

| Part | Value / Type | Notes                           |
|------|--------------|---------------------------------|
| MOSFET | IRLZ44N | Gate driven from GPIO23         |
| Gate resistor | 100–220 Ω | Limits spikes                   |
| Gate pulldown | 47–100 kΩ | Ensures OFF at boot             |
| Flyback diode | 1N5819 Schottky | Cathode to +5 V, anode to drain |
| Capacitor | 220 µF electrolytic | Close to fan, suppresses spikes |
| Fan | 30 mm 5 V (3010) | Cooling                         |

---

## 🧩 Passives & Misc

- Capacitors: 220 µF electrolytic (smoothing)  
- Resistors: 0.25 W, 1% (general purpose)  
- Connectors, wiring, headers  

---

## 💡 Notes

- **Why both step-up and step-down before TP4056?**  
  Solar panel voltage fluctuates → boosting ensures stable headroom, then regulated down to 5 V → TP4056 input stays within safe range.  
- Place **flyback diode** and **capacitor** close to the fan for stability.  
- Use **Schottky diodes** for efficiency (low forward voltage drop).  
