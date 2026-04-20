# PlantSensor

ESP32-C3 LoRa plant monitor. Wakes every 15 minutes from deep sleep, reads soil moisture (capacitive), ambient light (LDR), temperature and humidity (DHT11), then transmits via LoRa 868 MHz to a combined receiver ([MailboxSensor](https://github.com/Chippen4713/MailboxSensor)).

## Features

- Deep sleep between readings (15 min interval, configurable)
- Sequence number persisted across deep sleep via RTC_DATA_ATTR
- ACK protocol — waits for receiver acknowledgment
- RGB LED status blinks (red = dry/dark, yellow = ok, green = good)
- Calibration constants for soil sensor (dry/wet raw ADC values)
- Packet format: `P,<seq>,<batteryMv>,<soilRaw>,<lightRaw>,<tempCx10>,<humPct>`

## Hardware

| Component | GPIO |
|-----------|------|
| Capacitive soil sensor | 0 (ADC) |
| DHT11 | 1 |
| LDR | 2 (ADC) |
| LoRa SCK | 4 |
| LoRa MISO | 5 |
| LoRa MOSI | 6 |
| LoRa SS | 7 |
| LoRa RST | 8 |
| LoRa DIO0 | 3 |
| LED Red | 9 |
| LED Green | 10 |

## Sensor Calibration

Edit these constants in `Plant_sensor.ino` to match your soil sensor:

```cpp
const int SOIL_DRY   = 2876;  // ADC reading in dry air
const int SOIL_WET   = 1941;  // ADC reading fully submerged
const int LIGHT_DARK   = 4095;
const int LIGHT_BRIGHT = 1130;
```

## Setup

Flash `Plant_sensor/Plant_sensor.ino` to an ESP32-C3.

### FQBN
```
esp32:esp32:esp32c3
```

### Libraries required
- `LoRa` by Sandeep Mistry
- `DHT sensor library` by Adafruit
- `SPI` (bundled with ESP32 Arduino core)

## Home Assistant

Data is received and published to MQTT by the [MailboxSensor](https://github.com/Chippen4713/MailboxSensor) receiver node, which includes full MQTT auto-discovery for plant entities (soil %, light %, temperature, humidity, battery, RSSI).
