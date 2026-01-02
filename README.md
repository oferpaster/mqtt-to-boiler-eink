# Boiler Temperature e-Paper Display (ESP32 + Waveshare 2.66\" G)

A low-power **ESP32-based boiler temperature display** using a **Waveshare 2.66\" tri-color e-Paper (G)** panel.
The device subscribes to **Home Assistant (MQTT)** temperature updates and displays them in a clean, high-contrast UI designed for long battery life.

---

## ✨ Features

- 📡 MQTT integration (Home Assistant compatible)
- 🖥 Waveshare 2.66\" e-Paper Module (G)
- 🔄 Landscape layout with centered temperature
- 🎨 Header color changes by temperature range
- 🔼🔽 Trend arrows (up/down)
- 🧊 Smooth color transition animation
- 💤 e-Paper sleep after each update
- 🔐 Secrets kept out of Git via `secrets.ini`

---

## 🧰 Hardware

- ESP32 Dev Board (ESP-WROOM-32)
- Waveshare 2.66inch e-Paper Module (G) – SKU 26337
- USB power (battery optimization planned)

---

## 🔌 Wiring (ESP32 → Waveshare)

| Waveshare | ESP32          |
| --------- | -------------- |
| VCC       | 3.3V           |
| GND       | GND            |
| DIN       | GPIO 23 (MOSI) |
| CLK       | GPIO 18 (SCK)  |
| CS        | GPIO 5         |
| DC        | GPIO 17        |
| RST       | GPIO 16        |
| BUSY      | GPIO 4         |
| PWR       | GPIO 2         |

> ⚠️ **Important:**  
> The display must be powered from **3.3V**, **not 5V**.

---

## 🧠 Software Stack

- **PlatformIO**
- **Arduino framework**
- **PubSubClient** (MQTT)
- **Custom Waveshare driver** (adapted for ESP32)
- Home Assistant (MQTT broker)

---

## 📦 Project Structure

```
.
├── src/
│   └── main.cpp
├── lib/
│   └── WaveshareEPD/
├── include/
│   └── avr/pgmspace.h
├── secrets.ini
├── platformio.ini
└── README.md
```

---

## 🔐 Configuration

### secrets.ini (not committed)

```ini
[secrets]
WIFI_SSID = MyWifi
WIFI_PASS = MyPassword!
MQTT_HOST = 192.168.1.10
MQTT_PORT = 1883
MQTT_USER = mqttUser
MQTT_PASS = mqttPassword!
```

---

## 🏠 Home Assistant Automation

```yaml
alias: Publish Boiler Temp Int to MQTT
trigger:
  - platform: state
    entity_id: sensor.boiler_temperature
condition:
  - condition: template
    value_template: >
      {{ states('sensor.boiler_temperature') not in
         ['unknown','unavailable','none',''] }}
action:
  - service: mqtt.publish
    data:
      topic: boiler/temp_int
      payload: "{{ states('sensor.boiler_temperature') | float | round(0) | int }}"
      retain: true
mode: single
```

---

## 📜 License

MIT License
